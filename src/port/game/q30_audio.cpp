// 2.55 and 3.0 music and sound: the music objects of the AMUS section, what plays on a
// DMap or screen, the crossfade mixer, OGG sound effects and the sounds and tunes past
// 2.53's limits. Ported from upstream's advanced_music.cpp, music_playback.cpp,
// zcmixer.cpp and scripting/types/musicdata.cpp. See q30_audio.h.

#include "precompiled.h" //always first

#include <map>
#include <string>
#include <vector>

#include "q30.h"
#include "q30_audio.h"

#include "zc_alleg.h"
#include "zelda.h"
#include "zcmusic.h"
#include "zcmusic_port.h"
#include "zc_malloc.h"

#ifndef _MSC_VER
#define stricmp strcasecmp
#endif

extern dmap     *DMaps;
extern SAMPLE   customsfxdata[WAV_COUNT];
extern unsigned char customsfxflag[WAV_COUNT>>3];
extern word     quest_header_zelda_version;
extern int      currdmap;
extern bool     sfx_init(int index);
extern bool     sfx_allocated(int index);
int             q30_sfx_voice(int index);       // zc_sys.cpp
void            q30_sfx_set_touched(int index); // zc_sys.cpp
void            music_stop();
void            playLevelMusic();
void            jukebox(int index);

namespace
{

const int BUILTIN_MIDI_MIN = -7;        // upstream's BUILTIN_MIDI_*: -(midi+1) is the built-in's index
const int MIDIOFFSET_ZSCRIPT = ZC_MIDI_COUNT - 1;
const int UPSTREAM_MAXMIDIS = ZC_MIDI_COUNT + 512;
const int MUSIC_UPDATE_SCREEN = 0, MUSIC_UPDATE_DMAP = 1, MUSIC_UPDATE_LEVEL = 2, MUSIC_UPDATE_NEVER = 3;
const int MUSIC_UPDATE_FLAG_REVERT = 2;
const long FULL_VOLUME = 10000 * 100;   // upstream's usr_music_volume at 100%

struct Enhanced
{
    std::string path;
    long loop_start, loop_end, xfade_in, xfade_out;
    byte track;
    Enhanced() : loop_start(0), loop_end(0), xfade_in(0), xfade_out(0), track(0) {}
};

struct Music
{
    std::string name;
    Enhanced enhanced;
    short midi;
    Music() : midi(0) {}
};

std::vector<Music> g_music;                     // ids count from 1
std::map<int, long> g_screen_music;             // screen index -> id (only ids > 0)
std::map<int, int>  g_dmap_music;               // DMap -> id (DMap v25+, or a v18+ DMap with loop points)
std::map<int, bool> g_dmap_new_format;

bool g_engine_music_active = false;
size_t g_active_music = 0;
int g_update_cond = MUSIC_UPDATE_SCREEN;
int g_update_flags = 0;
int g_music_dmap = -1;

long g_usr_music_volume = FULL_VOLUME;
long g_usr_sfx_volume = FULL_VOLUME;

// The crossfade state (upstream's ZCMIXER).
struct Mixer
{
    ZCMUSIC *newtrack, *oldtrack;
    int fadeinframes, fadeinmaxframes, fadeindelay;
    int fadeoutframes, fadeoutmaxframes, fadeoutdelay;
} g_mix;

// ---- sounds 256+ and OGG sounds -----------------------------------------------------

SAMPLE g_high_sfx[Q30_MAX_SFX - WAV_COUNT];     // cleared with q30_sfx_clear()

struct OggSound
{
    std::vector<byte> bytes;
    int bytes_decoded;
    unsigned long last_used;
    OggSound() : bytes_decoded(0), last_used(0) {}
};

std::map<int, OggSound> g_ogg_sounds;
std::map<int, int> g_sfx_alias;         // sound id past 255 -> slot below 256 (while loading)
unsigned long g_ogg_clock = 0;
size_t g_ogg_decoded_bytes = 0;

// OGG sounds this small are decoded as the quest loads (the usual short effects); longer
// ones when first played, which holds the game up for a moment that one time.
const size_t OGG_EAGER_BYTES = 96 * 1024;

// Most memory decoded OGG sounds may hold. The player is a 32-bit process and a quest can
// keep hours of audio this way; the bytes stay compressed until a sound is played.
const size_t OGG_DECODED_BUDGET = 128 * 1024 * 1024;

// ---- tunes from 252 up ----------------------------------------------------------------

std::vector<zctune> g_high_tunes;

zctune *high_tune(int index)
{
    if(index < MAXMIDIS)
        return NULL;

    size_t at = (size_t)(index - MAXMIDIS);
    return at < g_high_tunes.size() ? &g_high_tunes[at] : NULL;
}

bool is_new_quest()
{
    return quest_header_zelda_version >= 0x255;
}

int clamp_int(int v, int low, int high)
{
    return v < low ? low : (v > high ? high : v);
}

// ---- volume --------------------------------------------------------------------------

int music_volume_base()
{
    if(g_usr_music_volume == FULL_VOLUME)
        return emusic_volume;

    return (int)(((int64_t)emusic_volume * g_usr_music_volume) / 10000 / 100);
}

int get_emusic_volume()
{
    int v = music_volume_base();

    if(!zcmusic)
        return v;

    return (int)(((int64_t)v * zcmusic_port_fadevolume(zcmusic)) / 10000);
}

// Starts a just-loaded track at 'volume'. (A module renders its first start at 255 and
// lets the voice volume do the rest, so fades are heard straight away.)
void start_track(ZCMUSIC *m, int volume)
{
    zcmusic_play(m, 255);
    zcmusic_set_volume(m, volume);
}

// ---- files --------------------------------------------------------------------------

const char *base_name(const char *path)
{
    const char *name = path;

    for(const char *p = path; *p; ++p)
    {
        if(*p == '/' || *p == '\\')
            name = p + 1;
    }

    return name;
}

// The folder a path is in, with its last slash.
std::string folder_of(const char *path)
{
    const char *name = base_name(path);
    return std::string(path, name - path);
}

// True if file 'name' exists in 'dir' (dir ends in a slash, or is empty for the current
// folder), compared without regard to case.
bool find_in_dir(const std::string &dir, const std::string &name, std::string &found)
{
    std::string exact = dir + name;

    if(exists(exact.c_str()))
    {
        found = exact;
        return true;
    }

    std::string pattern = dir + "*";
    struct al_ffblk info;

    if(al_findfirst(pattern.c_str(), &info, FA_ALL) != 0)
        return false;

    bool ok = false;

    do
    {
        if(stricmp(info.name, name.c_str()) == 0)
        {
            found = dir + info.name;
            ok = true;
            break;
        }
    }
    while(al_findnext(&info) == 0);

    al_findclose(&info);
    return ok;
}

std::string with_slash(std::string dir)
{
    if(!dir.empty() && dir[dir.size() - 1] != '/' && dir[dir.size() - 1] != '\\')
        dir += '/';

    return dir;
}

// A sub-folder of 'dir' named 'name', any case. Appends it to dirs if it exists.
void add_sub_folder(const std::string &dir, const std::string &name, std::vector<std::string> &dirs)
{
    std::string found;

    if(find_in_dir(dir, name, found) && file_exists(found.c_str(), FA_DIREC | FA_RDONLY | FA_ARCH | FA_HIDDEN, NULL))
        dirs.push_back(with_slash(found));
}

// Upstream's zcmusic_load_for_quest: the exe's folder, the quest's folder, each one's
// "<quest>_music" and "music" folders, then the sub-folders of the quest's folder.
ZCMUSIC *load_for_quest(const char *filename)
{
    if(!filename || !filename[0])
        return NULL;

    char exe_path[2048];
    get_executable_name(exe_path, sizeof(exe_path));
    std::string exe_dir = folder_of(exe_path);
    const char *quest = qstpath ? qstpath : "";
    std::string quest_dir = folder_of(quest);
    std::string music_folder = std::string(base_name(quest)) + "_music";

    std::vector<std::string> dirs;
    dirs.push_back(exe_dir);
    dirs.push_back(quest_dir);
    add_sub_folder(exe_dir, music_folder, dirs);
    add_sub_folder(exe_dir, "music", dirs);
    add_sub_folder(quest_dir, music_folder, dirs);
    add_sub_folder(quest_dir, "music", dirs);

    {
        std::string pattern = quest_dir + "*";
        struct al_ffblk info;

        if(al_findfirst(pattern.c_str(), &info, FA_ALL) == 0)
        {
            do
            {
                if((info.attrib & FA_DIREC) && info.name[0] != '.')
                    dirs.push_back(with_slash(quest_dir + info.name));
            }
            while(al_findnext(&info) == 0);

            al_findclose(&info);
        }
    }

    for(size_t i = 0; i < dirs.size(); ++i)
    {
        std::string found;

        if(!find_in_dir(dirs[i], filename, found))
            continue;

        ZCMUSIC *m = zcmusic_port_load_whole(found.c_str());

        if(m)
            return m;
    }

    return NULL;
}

// ---- the mixer (upstream's zcmixer.cpp) ----------------------------------------------

void drop_old_track()
{
    if(g_mix.oldtrack)
    {
        zcmusic_stop(g_mix.oldtrack);
        zcmusic_unload_file(g_mix.oldtrack);
    }
}

void clear_current_music_data()
{
    g_engine_music_active = false;
    g_active_music = 0;
}

// Upstream's try_zcmusic.
bool try_zcmusic(const char *filename, int track, int midi, int vol, int fadeoutframes)
{
    clear_current_music_data();
    ZCMUSIC *newzcmusic = load_for_quest(filename);

    if(newzcmusic != NULL)
    {
        zcmusic_port_set_fadevolume(newzcmusic, 10000);
        zcmusic_port_set_fadeoutframes(newzcmusic, fadeoutframes);
        g_mix.newtrack = newzcmusic;

        zcmusic_stop(zcmusic);
        zcmusic_unload_file(zcmusic);
        stop_midi();

        zcmusic = newzcmusic;
        start_track(zcmusic, vol);

        if(track > 0)
            zcmusic_change_track(zcmusic, track);

        return true;
    }

    if(midi > -1000)
        jukebox(midi);

    return false;
}

// Upstream's play_enh_music_crossfade.
bool play_crossfade(const char *name, int track, int vol, int fadein, int fadeout, int middle, long startpos, bool revert)
{
    clear_current_music_data();
    double fadeoutpct = 1.0;

    if(g_mix.newtrack != NULL)
        fadeoutpct = (double)zcmusic_port_fadevolume(g_mix.newtrack) / 10000.0;

    ZCMUSIC *oldold = g_mix.oldtrack;
    bool ret = false;

    g_mix.oldtrack = zcmusic;
    zcmusic = NULL;
    g_mix.newtrack = NULL;

    if(name != NULL)
    {
        ret = try_zcmusic(name, track, -1000, vol, fadeout);

        if(ret)
            g_mix.newtrack = zcmusic;
    }

    if(name == NULL || ret)
    {
        g_mix.fadeinframes = fadein;
        g_mix.fadeinmaxframes = fadein;
        g_mix.fadeoutframes = (int)(fadeout * fadeoutpct);

        if(g_mix.fadeoutframes < 1)
            g_mix.fadeoutframes = 1;

        g_mix.fadeoutmaxframes = fadeout;

        if(middle < 0)
        {
            g_mix.fadeindelay = 0;
            g_mix.fadeoutdelay = -middle;
        }
        else
        {
            g_mix.fadeindelay = middle;
            g_mix.fadeoutdelay = 0;
        }

        if(name != NULL && startpos > 0)
            zcmusic_set_curpos(g_mix.newtrack, startpos);

        if(g_mix.oldtrack != NULL)
            zcmusic_port_set_fadevolume(g_mix.oldtrack, (name == NULL || fadeout > 0) ? 10000 : 0);

        if(g_mix.newtrack != NULL)
            zcmusic_port_set_fadevolume(g_mix.newtrack, fadein > 0 ? 0 : 10000);
    }
    else if(revert)
    {
        zcmusic = g_mix.oldtrack;
        g_mix.newtrack = NULL;
        g_mix.oldtrack = NULL;
    }

    if(oldold != NULL)
    {
        if(g_mix.newtrack == NULL && g_mix.oldtrack == NULL)
        {
            g_mix.oldtrack = oldold;
            int oldfade = zcmusic_port_fadeoutframes(oldold);

            if(oldfade > 0)
            {
                g_mix.fadeoutframes = (int)(oldfade * fadeoutpct);

                if(g_mix.fadeoutframes < 1)
                    g_mix.fadeoutframes = 1;

                g_mix.fadeoutmaxframes = oldfade;
                zcmusic_port_set_fadevolume(oldold, 10000);
                zcmusic_port_set_fadeoutframes(oldold, 0);
            }
        }
        else
        {
            zcmusic_stop(oldold);
            zcmusic_unload_file(oldold);
        }
    }

    return ret;
}

// ---- a music object (upstream's AdvancedMusic) ----------------------------------------

bool enhanced_is_empty(const Enhanced &e)
{
    return e.path.empty();
}

// zcmusic->filename holds only the file name.
bool enhanced_is_playing(const Enhanced &e)
{
    if(zcmusic == NULL || e.path.empty())
        return false;

    if(strcmp(zcmusic->filename, base_name(e.path.c_str())) != 0)
        return false;

    if(zcmusic->type == ZCMF_GME)
    {
        int tracks = zcmusic_get_tracks(zcmusic);

        if(tracks < 1)
            tracks = 1;

        if(zcmusic->track != clamp_int(e.track, 0, tracks - 1))
            return false;
    }

    return true;
}

bool enhanced_play(const Enhanced &e)
{
    if(e.path.empty())
        return false;

    if(enhanced_is_playing(e))
    {
        zcmusic_port_set_fadeoutframes(zcmusic, (int)e.xfade_out);
        zcmusic_set_loop(zcmusic, e.loop_start / 10000.0, e.loop_end / 10000.0);
        return true;
    }

    int fadeoutframes = zcmusic ? zcmusic_port_fadeoutframes(zcmusic) : 0;

    if(e.xfade_in > 0 || fadeoutframes > 0)
    {
        if(!play_crossfade(e.path.c_str(), e.track, get_emusic_volume(), (int)e.xfade_in, fadeoutframes, 0, 0, false))
            return false;

        if(zcmusic)
        {
            zcmusic_port_set_fadeoutframes(zcmusic, (int)e.xfade_out);
            zcmusic_set_loop(zcmusic, e.loop_start / 10000.0, e.loop_end / 10000.0);
        }

        return true;
    }

    if(zcmusic != NULL)
    {
        zcmusic_stop(zcmusic);
        zcmusic_unload_file(zcmusic);
        zcmusic = NULL;
        g_mix.newtrack = NULL;
    }

    zcmusic = load_for_quest(e.path.c_str());
    g_mix.newtrack = zcmusic;

    if(!zcmusic)
        return false;

    stop_midi();
    zcmusic_port_set_fadevolume(zcmusic, 10000);
    zcmusic_port_set_fadeoutframes(zcmusic, (int)e.xfade_out);
    start_track(zcmusic, get_emusic_volume());

    int tracks = zcmusic_get_tracks(zcmusic);

    if(tracks < 1)
        tracks = 1;

    zcmusic_change_track(zcmusic, clamp_int(e.track, 0, tracks - 1));
    zcmusic_set_loop(zcmusic, e.loop_start / 10000.0, e.loop_end / 10000.0);
    return true;
}

// The index in 'tunes' (or the high table), -1: none.
int real_midi(const Music &m)
{
    if(m.midi >= BUILTIN_MIDI_MIN && m.midi < 0)
        return -(m.midi + 1);

    if(m.midi <= 0)
        return -1;

    return clamp_int(m.midi + MIDIOFFSET_ZSCRIPT, 0, UPSTREAM_MAXMIDIS - 1);
}

void music_play(size_t id)
{
    if(enhanced_play(g_music[id - 1].enhanced))
    {
        g_engine_music_active = false;
        g_active_music = id;
        return;
    }

    Music &music = g_music[id - 1];
    int fadeoutframes = zcmusic ? zcmusic_port_fadeoutframes(zcmusic) : 0;
    int m = real_midi(music);

    if(m < 0)
    {
        if(fadeoutframes > 0 && zcmusic)
            play_crossfade(NULL, music.enhanced.track, get_emusic_volume(), (int)music.enhanced.xfade_in, fadeoutframes, 0, 0, false);
        else
            music_stop();
    }
    else if(!q30_jukebox_high(m))
        jukebox(m);

    g_engine_music_active = false;
    g_active_music = id;
}

bool music_is_playing(size_t id, bool same_song)
{
    if(same_song)
    {
        if(enhanced_is_playing(g_music[id - 1].enhanced))
            return true;

        if(real_midi(g_music[id - 1]) == currmidi)
            return true;
    }

    return g_active_music == id;
}

// ---- OGG sound effects ----------------------------------------------------------------

void drop_decoded(OggSound &o, SAMPLE &s)
{
    g_ogg_decoded_bytes -= (size_t)o.bytes_decoded;
    o.bytes_decoded = 0;

    if(s.data)
    {
        free(s.data);
        s.data = NULL;
    }

    s.len = 0;
}

// Keeps the decoded sounds under the budget; a sound with a voice stays.
void trim_decoded(size_t want)
{
    while(g_ogg_decoded_bytes + want > OGG_DECODED_BUDGET)
    {
        int oldest_id = -1;
        unsigned long oldest = 0;

        for(std::map<int, OggSound>::iterator it = g_ogg_sounds.begin(); it != g_ogg_sounds.end(); ++it)
        {
            if(it->second.bytes_decoded && !sfx_allocated(it->first) && (oldest_id < 0 || it->second.last_used < oldest))
            {
                oldest_id = it->first;
                oldest = it->second.last_used;
            }
        }

        if(oldest_id < 0)
            return;

        drop_decoded(g_ogg_sounds[oldest_id], *q30_sfx_slot(oldest_id));
    }
}

bool decode_ogg_sound(int id, OggSound &o)
{
    SAMPLE *s = q30_sfx_slot(id);

    if(!s)
        return false;

    short *pcm = NULL;
    int channels = 0, rate = 0, frames = 0;

    if(!zcmusic_port_decode_ogg_sample(o.bytes.empty() ? NULL : &o.bytes[0], (long)o.bytes.size(), &pcm, &channels, &rate, &frames) || !pcm)
    {
        al_trace("OGG sound %d could not be decoded\n", id);
        return false;
    }

    int out_channels = channels >= 2 ? 2 : 1;
    size_t bytes = (size_t)frames * out_channels * 2;
    trim_decoded(bytes);

    word *data = (word *)malloc(bytes);

    if(!data)
    {
        free(pcm);
        return false;
    }

    for(int i = 0; i < frames; ++i)
    {
        for(int c = 0; c < out_channels; ++c)
            data[(size_t)i * out_channels + c] = (word)(pcm[(size_t)i * channels + c] ^ 0x8000);
    }

    free(pcm);

    if(s->data)
        free(s->data);

    s->data = data;
    s->bits = 16;
    s->stereo = out_channels == 2;
    s->freq = rate;
    s->priority = 128;
    s->len = (unsigned long)frames;
    s->loop_start = 0;
    s->loop_end = (unsigned long)frames;
    s->param = 0;
    o.bytes_decoded = (int)bytes;
    g_ogg_decoded_bytes += bytes;

    if(id < WAV_COUNT)
        set_bit(customsfxflag, id - 1, 1);

    return true;
}

} // namespace

// ---- the AMUS section -----------------------------------------------------------------

int q30_read_advmusic(Q255Section &s)
{
    if(s.version > 1)
        return qe_version;

    word count = s.igetw();

    if(s.failed)
        return qe_invalid;

    if(s.keep)
        g_music.clear();

    for(int i = 0; i < count; ++i)
    {
        std::string name = s.getwstr();
        std::string path = s.getcstr();
        Enhanced e;

        if(!path.empty())
        {
            e.path = path;
            e.loop_start = s.igetl_s();
            e.loop_end = s.igetl_s();
            e.xfade_in = s.igetl_s();
            e.xfade_out = s.igetl_s();
            e.track = s.getc();
        }

        short midi = s.igetw_s();

        if(s.failed)
            return qe_invalid;

        if(s.keep)
        {
            Music m;
            m.name = name;
            m.enhanced = e;
            m.midi = midi;
            g_music.push_back(m);
        }
    }

    if(s.keep)
    {
        q30_finish_music_tables();
        al_trace("AMUS: %d music objects\n", (int)g_music.size());
    }

    return s.failed ? qe_invalid : 0;
}

void q30_audio_clear_screens()
{
    g_screen_music.clear();
}

void q30_audio_clear_dmaps()
{
    g_music.clear();
    g_dmap_music.clear();
    g_dmap_new_format.clear();
    clear_current_music_data();
    g_update_cond = MUSIC_UPDATE_SCREEN;
    g_update_flags = 0;
    g_music_dmap = -1;
    g_usr_music_volume = FULL_VOLUME;
    g_usr_sfx_volume = FULL_VOLUME;
}

int q30_music_add(const char *name, const char *path, long loop_start, long loop_end, long xfade_in, long xfade_out, byte track, short midi)
{
    Music m;
    m.name = name ? name : "";
    m.enhanced.path = path ? path : "";
    m.enhanced.loop_start = loop_start;
    m.enhanced.loop_end = loop_end;
    m.enhanced.xfade_in = xfade_in;
    m.enhanced.xfade_out = xfade_out;
    m.enhanced.track = track;
    m.midi = midi;
    g_music.push_back(m);
    return (int)g_music.size();
}

void q30_set_dmap_music(int dmap, int music, bool new_format)
{
    if(dmap < 0 || dmap >= MAXDMAPS)
        return;

    g_dmap_music[dmap] = music;
    g_dmap_new_format[dmap] = new_format;
}

void q30_set_screen_music(int index, long music)
{
    if(music > 0)
        g_screen_music[index] = music;
    else
        g_screen_music.erase(index);
}

void q30_finish_music_tables()
{
    for(std::map<int, int>::iterator it = g_dmap_music.begin(); it != g_dmap_music.end(); ++it)
    {
        int id = it->second;

        if(!g_dmap_new_format[it->first] || id <= 0 || (size_t)id > g_music.size())
            continue;

        dmap &d = DMaps[it->first];
        const Music &m = g_music[(size_t)id - 1];

        if(!m.enhanced.path.empty())
        {
            strncpy(d.tmusic, base_name(m.enhanced.path.c_str()), sizeof(d.tmusic) - 1);
            d.tmusic[sizeof(d.tmusic) - 1] = 0;
            d.tmusictrack = m.enhanced.track;
        }

        // the "custom tune k is DMap music k+3" numbers 2.53 uses
        if(m.midi > 0)
            d.midi = (byte)(m.midi + 3 < 255 ? m.midi + 3 : 0);
        else if(m.midi == -5)           // BUILTIN_MIDI_OVERWORLD
            d.midi = 1;
        else if(m.midi == -1)           // BUILTIN_MIDI_DUNGEON
            d.midi = 2;
        else if(m.midi == -4)           // BUILTIN_MIDI_LEVEL9
            d.midi = 3;
    }
}

// ---- sound slots ----------------------------------------------------------------------

void q30_sfx_clear()
{
    for(std::map<int, OggSound>::iterator it = g_ogg_sounds.begin(); it != g_ogg_sounds.end(); ++it)
    {
        if(it->first >= WAV_COUNT || it->second.bytes_decoded)
        {
            SAMPLE *s = q30_sfx_slot(it->first);

            if(s && s->data)
            {
                free(s->data);
                s->data = NULL;
            }
        }
    }

    g_ogg_sounds.clear();
    g_ogg_decoded_bytes = 0;

    for(int i = 0; i < Q30_MAX_SFX - WAV_COUNT; ++i)
    {
        if(g_high_sfx[i].data)
            free(g_high_sfx[i].data);

        memset(&g_high_sfx[i], 0, sizeof(g_high_sfx[i]));
        g_high_sfx[i].bits = 8;
        g_high_sfx[i].freq = 22050;
        g_high_sfx[i].priority = 128;
    }
}

SAMPLE *q30_sfx_slot(int id)
{
    if(id <= 0 || id >= Q30_MAX_SFX)
        return NULL;

    return id < WAV_COUNT ? &customsfxdata[id] : &g_high_sfx[id - WAV_COUNT];
}

void q30_sfx_store_ogg(int id, const byte *data, dword len)
{
    if(id <= 0 || id >= Q30_MAX_SFX)
        return;

    OggSound &o = g_ogg_sounds[id];
    const bool eager = len <= OGG_EAGER_BYTES;
    o.bytes.assign(data, data + len);

    if(eager)
        decode_ogg_sound(id, o);
}

bool q30_sfx_is_ogg(int id)
{
    return is_new_quest() && g_ogg_sounds.find(id) != g_ogg_sounds.end();
}

int q30_sfx_alias(int high)
{
    SAMPLE *real = q30_sfx_slot(high);

    if(!real || (!real->data && g_ogg_sounds.find(high) == g_ogg_sounds.end()))
        return 0;

    std::map<int, int>::const_iterator known = g_sfx_alias.find(high);

    if(known != g_sfx_alias.end())
        return known->second;

    for(int i = WAV_COUNT - 1; i > 0; --i)
    {
        SAMPLE *slot = q30_sfx_slot(i);

        if(slot->data || g_ogg_sounds.find(i) != g_ogg_sounds.end())
            continue;

        std::map<int, OggSound>::iterator ogg = g_ogg_sounds.find(high);

        if(ogg != g_ogg_sounds.end())
        {
            OggSound &copy = g_ogg_sounds[i];
            copy.bytes = ogg->second.bytes;
        }
        else
        {
            size_t bytes = (real->bits == 8 ? 1 : 2) * (real->stereo ? 2 : 1) * real->len;
            void *data = malloc(bytes ? bytes : 1);

            if(!data)
                return 0;

            memcpy(data, real->data, bytes);
            *slot = *real;
            slot->data = data;
        }

        set_bit(customsfxflag, i - 1, 1);
        g_sfx_alias[high] = i;
        return i;
    }

    return 0;
}

void q30_sfx_finish_aliases()
{
    g_sfx_alias.clear();
}

const SAMPLE *q30_sfx_sample(int id)
{
    if(!is_new_quest())
        return NULL;

    SAMPLE *s = q30_sfx_slot(id);

    if(!s)
        return NULL;

    std::map<int, OggSound>::iterator it = g_ogg_sounds.find(id);

    if(it != g_ogg_sounds.end())
    {
        it->second.last_used = ++g_ogg_clock;

        if(!s->data && !decode_ogg_sound(id, it->second))
            return NULL;
    }

    return s;
}

// ---- tunes from 252 up ----------------------------------------------------------------

void q30_tunes_clear()
{
    for(size_t i = 0; i < g_high_tunes.size(); ++i)
        g_high_tunes[i].reset();

    g_high_tunes.clear();
}

void q30_store_tune(int index, const char *title, long start, long loop_start, long loop_end, short loop, short volume, byte flags, byte format, void *midi)
{
    if(index < MAXCUSTOMTUNES)
        return;

    size_t at = (size_t)(index - MAXCUSTOMTUNES);

    if(at >= g_high_tunes.size())
        g_high_tunes.resize(at + 1);

    zctune &t = g_high_tunes[at];
    strncpy(t.title, title ? title : "", sizeof(t.title) - 1);
    t.title[sizeof(t.title) - 1] = 0;
    t.start = start;
    t.loop_start = loop_start;
    t.loop_end = loop_end;
    t.loop = loop;
    t.volume = volume;
    t.flags = flags;
    t.format = format;
    t.data = midi;
}

bool q30_jukebox_high(int index)
{
    if(index < MAXMIDIS)
        return false;

    zctune *t = high_tune(index);
    music_stop();

    if(t == NULL || t->data == NULL)
        return true;

    int mixed = (zc_min(t->volume, 255) * zc_min(midi_volume >> 1, 255)) >> 8;
    set_volume(-1, mixed);
    play_midi((MIDI *)t->data, t->loop);

    if(t->start > 0)
        midi_seek(t->start);

    midi_loop_end = t->loop_end;
    midi_loop_start = t->loop_start;
    currmidi = index;
    set_volume(digi_volume, (zc_min(t->volume, 255) * zc_min(q30_midi_volume(midi_volume), 255)) >> 8);
    return true;
}

// ---- hooks ----------------------------------------------------------------------------

bool q30_play_level_music()
{
    if(!is_new_quest())
        return false;

    std::map<int, long>::const_iterator screen = g_screen_music.find(currmap * MAPSCRS + currscr);
    long m;

    if(screen != g_screen_music.end())
        m = screen->second;
    else if(tmpscr->screen_midi == -2)
    {
        music_stop();
        return true;
    }
    else if(tmpscr->screen_midi != -1)
        return false;   // an older screen's music number: as 2.53 does it
    else
    {
        std::map<int, bool>::const_iterator f = g_dmap_new_format.find(currdmap);

        if(f == g_dmap_new_format.end())
            return false;

        m = g_dmap_music[currdmap];
    }

    // A music change only happens as often as the quest's refresh setting allows.
    bool changing_allowed = true;

    switch(g_update_cond)
    {
    case MUSIC_UPDATE_DMAP:
        changing_allowed = currdmap != g_music_dmap;
        break;

    case MUSIC_UPDATE_LEVEL:
        changing_allowed = g_music_dmap < 0 || DMaps[currdmap].level != DMaps[g_music_dmap].level;
        break;

    case MUSIC_UPDATE_NEVER:
        changing_allowed = false;
        break;
    }

    g_music_dmap = currdmap;

    if(!changing_allowed && (zcmusic != NULL || midi_pos >= 0))
        return true;

    if(m > 0 && (size_t)m <= g_music.size())
        music_play((size_t)m);
    else
        music_stop();

    if(g_update_cond != MUSIC_UPDATE_SCREEN && (g_update_flags & MUSIC_UPDATE_FLAG_REVERT))
        g_update_cond = MUSIC_UPDATE_SCREEN;

    g_engine_music_active = true;
    return true;
}

void q30_audio_frame()
{
    int base = music_volume_base();

    if(g_mix.fadeinframes)
    {
        if(g_mix.fadeindelay)
        {
            --g_mix.fadeindelay;

            if(g_mix.newtrack != NULL)
                zcmusic_set_volume(g_mix.newtrack, 0);
        }
        else
        {
            --g_mix.fadeinframes;

            if(g_mix.newtrack != NULL)
            {
                int64_t pct = g_mix.fadeinmaxframes ? ((int64_t)g_mix.fadeinframes * 10000) / g_mix.fadeinmaxframes : 0;
                int fade = 10000 - (int)clamp_int((int)pct, 0, 10000);
                zcmusic_port_set_fadevolume(g_mix.newtrack, fade);
                zcmusic_set_volume(g_mix.newtrack, (int)(((int64_t)base * fade) / 10000));

                if(g_mix.fadeinframes == 0)
                    zcmusic_port_set_fadevolume(g_mix.newtrack, 10000);
            }
        }
    }

    if(g_mix.fadeoutframes)
    {
        if(g_mix.fadeoutdelay)
            --g_mix.fadeoutdelay;
        else
            --g_mix.fadeoutframes;

        if(g_mix.oldtrack != NULL)
        {
            int pct = 0;

            if(g_mix.fadeoutframes > 0 && g_mix.fadeoutmaxframes > 0)
                pct = clamp_int((int)(((int64_t)g_mix.fadeoutframes * 10000) / g_mix.fadeoutmaxframes), 0, 10000);

            zcmusic_port_set_fadevolume(g_mix.oldtrack, pct);
            zcmusic_set_volume(g_mix.oldtrack, (int)(((int64_t)base * pct) / 10000));
        }

        if(g_mix.fadeoutframes == 0)
        {
            drop_old_track();
            g_mix.oldtrack = NULL;
        }
    }
}

void q30_audio_music_stopped()
{
    if(!is_new_quest())
        return;

    drop_old_track();
    g_mix.oldtrack = NULL;
    g_mix.newtrack = NULL;
    g_mix.fadeinframes = 0;
    g_mix.fadeoutframes = 0;
    clear_current_music_data();
}

void q30_audio_pause(bool pause)
{
    if(g_mix.oldtrack)
        zcmusic_pause(g_mix.oldtrack, pause ? ZCM_PAUSE : ZCM_RESUME);
}

int q30_midi_volume(int volume)
{
    if(g_usr_music_volume == FULL_VOLUME || !is_new_quest())
        return volume;

    return (int)(((int64_t)volume * g_usr_music_volume) / 10000 / 100);
}

int q30_sfx_volume(int volume)
{
    if(g_usr_sfx_volume == FULL_VOLUME || !is_new_quest())
        return volume;

    return (int)(((int64_t)volume * g_usr_sfx_volume) / 1000000);
}

// ---- scripts --------------------------------------------------------------------------

void q30_play_music(long ref)
{
    if(ref == 0)
        music_stop();
    else if(ref == -1)
        playLevelMusic();
    else if(q30_music_exists(ref))
        music_play((size_t)ref);
}

void q30_play_midi(int midi)
{
    if(midi == 0)
    {
        music_stop();
        return;
    }

    int index = midi + MIDIOFFSET_ZSCRIPT;

    if(!q30_jukebox_high(index))
        jukebox(index);
}

bool q30_play_enhanced(const char *filename, int track)
{
    if(filename == NULL)
    {
        music_stop();
        return false;
    }

    return try_zcmusic(filename, track, -1000, get_emusic_volume(), 0);
}

bool q30_enh_crossfade(const char *filename, int track, int fadeout, int fadein, int middle, long startpos)
{
    // In upstream's call the script's "fadeout" lands in the fade-in parameter and vice versa.
    return play_crossfade(filename, track, get_emusic_volume(), fadeout, fadein, middle, startpos, filename != NULL);
}

long q30_enh_get_pos()
{
    return zcmusic_get_curpos(zcmusic);
}

void q30_enh_set_pos(long position)
{
    zcmusic_set_curpos(zcmusic, position);
}

void q30_enh_set_speed(long speed)
{
    zcmusic_set_speed(zcmusic, speed);
}

long q30_enh_get_length()
{
    return zcmusic_get_length(zcmusic);
}

void q30_enh_set_loop(double start, double end)
{
    zcmusic_set_loop(zcmusic, start, end);
}

void q30_adjust_music_volume(long perc)
{
    g_usr_music_volume = perc < 0 ? 0 : (perc > FULL_VOLUME ? FULL_VOLUME : perc);

    if(zcmusic != NULL && zcmusic->playing != ZCM_STOPPED)
        zcmusic_set_volume(zcmusic, get_emusic_volume());
    else if(currmidi >= 0)
    {
        int i = currmidi < MAXMIDIS ? currmidi : MAXMIDIS - 1;
        set_volume(digi_volume, (zc_min(tunes[i].volume, 255) * zc_min(q30_midi_volume(midi_volume), 255)) >> 8);
    }
}

void q30_adjust_sfx_volume(long perc)
{
    g_usr_sfx_volume = perc < 0 ? 0 : (perc > FULL_VOLUME ? FULL_VOLUME : perc);
}

void q30_sfx_ex(int id, long volume, long pan, long freq, long loop, bool restart)
{
    if(id <= 0 || id >= Q30_MAX_SFX)
        return;

    double percent = volume / 10000.0;

    if(percent < 0)
        percent = 0;
    else if(percent > 100)
        percent = 100;

    int real_pan = clamp_int((int)(pan / 10000) + 128, 0, 255);

    if(!restart && !sfx_allocated(id))
        return;

    if(!sfx_init(id))
        return;

    int voice = q30_sfx_voice(id);

    if(voice < 0)
        return;

    voice_set_playmode(voice, (loop / 10000) ? PLAYMODE_LOOP : PLAYMODE_PLAY);
    voice_set_pan(voice, real_pan);
    voice_set_volume(voice, clamp_int((int)(q30_sfx_volume(sfx_volume) * percent / 100.0), 0, 255));
    SAMPLE *s = voice_check(voice);

    if(s)
        voice_set_frequency(voice, freq < 0 ? s->freq : (int)freq);

    q30_sfx_set_touched(id);

    if(restart)
        voice_set_position(voice, 0);

    voice_start(voice);
}

long q30_sfx_completion(int id)
{
    if(id <= 0 || id >= Q30_MAX_SFX)
        return -10000;

    int voice = q30_sfx_voice(id);

    if(voice < 0)
        return -10000;

    int pos = voice_get_position(voice);
    SAMPLE *s = voice_check(voice);

    if(pos < 0 || !s || !s->len)
        return -10000;

    return (long)(((int64_t)pos * 1000000) / (int64_t)s->len);
}

bool q30_engine_music_active()
{
    return g_engine_music_active;
}

int q30_music_update_cond()
{
    return g_update_cond;
}

void q30_set_music_update_cond(int cond)
{
    g_update_cond = clamp_int(cond, 0, 255);
}

bool q30_music_update_flag(int index)
{
    return (g_update_flags >> index) & 1;
}

void q30_set_music_update_flag(int index, bool on)
{
    if(on)
        g_update_flags |= 1 << index;
    else
        g_update_flags &= ~(1 << index);
}

int q30_music_count()
{
    return (int)g_music.size();
}

int q30_music_by_name(const char *name)
{
    for(size_t i = 0; i < g_music.size(); ++i)
    {
        if(g_music[i].name == name)
            return (int)i + 1;
    }

    return 0;
}

bool q30_music_exists(long ref)
{
    return ref > 0 && (size_t)ref <= g_music.size();
}

// A changed music object restarts if it is what's playing.
static void replay_if_active(size_t id)
{
    bool ema = g_engine_music_active;
    music_play(id);
    g_engine_music_active = ema;
}

long q30_musicdata_get(long ref, int field)
{
    if(field == q30md_ID)
        return ref * 10000;

    if(!q30_music_exists(ref))
        return 0;

    const Music &m = g_music[(size_t)ref - 1];

    switch(field)
    {
    case q30md_IS_ACTIVE:
        return music_is_playing((size_t)ref, false) ? 10000 : 0;

    case q30md_MIDI:
        return m.midi * 10000L;

    case q30md_IS_ENHANCED:
        return enhanced_is_empty(m.enhanced) ? 0 : 10000;

    case q30md_TRACK:
        return m.enhanced.track * 10000L;

    case q30md_LOOPSTART:
        return m.enhanced.loop_start;

    case q30md_LOOPEND:
        return m.enhanced.loop_end;

    case q30md_XFADEIN:
        return m.enhanced.xfade_in * 10000L;

    case q30md_XFADEOUT:
        return m.enhanced.xfade_out * 10000L;
    }

    return 0;
}

void q30_musicdata_set(long ref, int field, long value)
{
    if(!q30_music_exists(ref))
        return;

    Music &m = g_music[(size_t)ref - 1];
    size_t id = (size_t)ref;

    switch(field)
    {
    case q30md_MIDI:
    {
        short v = (short)(value / 10000);

        if(m.midi != v)
        {
            m.midi = v;

            if(music_is_playing(id, false) && !zcmusic)
                replay_if_active(id);
        }

        break;
    }

    case q30md_TRACK:
    {
        byte v = (byte)(value / 10000);

        if(m.enhanced.track != v)
        {
            m.enhanced.track = v;

            if(music_is_playing(id, false) && zcmusic)
                replay_if_active(id);
        }

        break;
    }

    case q30md_LOOPSTART:
    case q30md_LOOPEND:
    {
        long &target = field == q30md_LOOPSTART ? m.enhanced.loop_start : m.enhanced.loop_end;

        if(target != value)
        {
            target = value;

            if(music_is_playing(id, false) && zcmusic)
                zcmusic_set_loop(zcmusic, m.enhanced.loop_start / 10000.0, m.enhanced.loop_end / 10000.0);
        }

        break;
    }

    case q30md_XFADEIN:
        m.enhanced.xfade_in = value / 10000;
        break;

    case q30md_XFADEOUT:
    {
        long v = value / 10000;

        if(m.enhanced.xfade_out != v)
        {
            m.enhanced.xfade_out = v;

            if(music_is_playing(id, false) && zcmusic)
                zcmusic_port_set_fadeoutframes(zcmusic, (int)v);
        }

        break;
    }
    }
}

const char *q30_musicdata_path(long ref)
{
    return q30_music_exists(ref) ? g_music[(size_t)ref - 1].enhanced.path.c_str() : "";
}

void q30_musicdata_set_path(long ref, const char *path)
{
    if(!q30_music_exists(ref))
        return;

    Music &m = g_music[(size_t)ref - 1];

    if(m.enhanced.path != path)
    {
        m.enhanced.path = path;

        if(music_is_playing((size_t)ref, false) && (zcmusic || path[0]))
            replay_if_active((size_t)ref);
    }
}

const char *q30_musicdata_name(long ref)
{
    return q30_music_exists(ref) ? g_music[(size_t)ref - 1].name.c_str() : "";
}

void q30_musicdata_set_name(long ref, const char *name)
{
    if(q30_music_exists(ref))
        g_music[(size_t)ref - 1].name = name;
}

long q30_screen_music(int index)
{
    std::map<int, long>::const_iterator it = g_screen_music.find(index);

    if(it != g_screen_music.end() && q30_music_exists(it->second))
        return it->second;

    if(it != g_screen_music.end())
        return -1;

    // no music object: what the 2.53 field says, as upstream reads an old screen
    if(index >= 0 && (size_t)index < TheMaps.size())
    {
        short m = TheMaps[index].screen_midi;

        if(m == -2 || m == 0)
            return m == -2 ? 0 : -1;
    }

    return -1;
}

void q30_set_screen_music_script(int index, long music)
{
    if(music != -1 && music != 0 && !q30_music_exists(music))
        return;

    if(q30_screen_music(index) == music)
        return;

    if(music > 0)
        g_screen_music[index] = music;
    else
        g_screen_music.erase(index);

    if(index >= 0 && (size_t)index < TheMaps.size())
        TheMaps[index].screen_midi = music == 0 ? -2 : -1;

    if(g_engine_music_active && index == currmap * MAPSCRS + currscr)
    {
        tmpscr->screen_midi = music == 0 ? -2 : -1;
        playLevelMusic();
    }
}

long q30_dmap_music(int dmap)
{
    std::map<int, int>::const_iterator it = g_dmap_music.find(dmap);
    return it == g_dmap_music.end() ? 0 : it->second;
}

void q30_set_dmap_music_script(int dmap, long music)
{
    if(dmap < 0 || dmap >= MAXDMAPS)
        return;

    if(music != 0 && !q30_music_exists(music))
        return;

    if(q30_dmap_music(dmap) == music)
        return;

    g_dmap_music[dmap] = (int)music;
    g_dmap_new_format[dmap] = true;
    q30_finish_music_tables();

    if(g_engine_music_active && dmap == currdmap && q30_screen_music(currmap * MAPSCRS + currscr) == -1)
        playLevelMusic();
}

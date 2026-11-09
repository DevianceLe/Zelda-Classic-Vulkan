// zplayer: the zcmusic API (src/zcmusic.h) on the music decoders under
// third_party/ (zcmusic_decoders.*) and zcport's audio streams. Replaces
// src/zcmusic.cpp in the zplayer build.
//
// src/zcmusic.cpp decodes with alogg (OGG), almp3 (MP3), DUMB 0.9.3 through
// aldumb (IT, XM, S3M, MOD) and Game_Music_Emu 0.3.0 (SPC, GBS, VGM, NSF,
// GYM), each feeding an Allegro audio stream that zcmusic_poll() refills.
// This file drives zcport's audio streams the same way, with the same
// parameters (16-bit unsigned samples):
//
//   format    stream                                  volume ('vol' of zcmusic_play())
//   OGG, MP3  zcmusic_bufsz KB per buffer, the        voice volume vol, pan 128; playing
//             file's rate, mono or stereo             again changes it
//   modules   zcmusic_bufsz * 1024 samples per        voice volume 255 (priority 255),
//             buffer, 44100 Hz stereo                 rendered at vol / 255; playing
//                                                     again changes it
//   GME       2048 samples per buffer, 44100 Hz       voice volume vol, set when the
//             stereo                                  music starts only
//
// What the game can observe is kept as src/zcmusic.cpp does it:
//
// - zcmusic_load_file() succeeds for the same files: the extension selects
//   the format and the file is opened the way the original opens it
//   (packfiles for OGG/MP3 and the tracker formats, stdio for the
//   Game_Music_Emu formats). ZScript's PlayEnhancedMusic() returns this
//   result, and the game falls back to MIDI when it fails. Which files the
//   decoders accept follows the original's: an OGG's Vorbis headers must be
//   in its first block (zcmusic_bufsz KB / 2), an MP3's first decodable frame
//   in the block that src/zcmusic.cpp hands to almp3. The decoders are
//   different libraries (or versions), so damaged files that one accepts
//   and the other rejects can still differ.
// - The MP3 loader's reads are repeated byte for byte: src/zcmusic.cpp
//   discards the first data block after the ID3 tag (or after the first 10
//   bytes) and fails when the second read finds nothing, so short MP3 files
//   do not load, and playback starts at the second block (about two
//   seconds in at 128 kbit/s with the default buffer size).
// - OGG and MP3 files start again at their end. src/zcmusic.cpp destroys the
//   stream and opens the file again (with the MP3 skip above); here the
//   decoder restarts on the same bytes, kept from the first pass, without a
//   gap and without opening the file on the timer thread. Tracker modules
//   loop by themselves (DUMB plays on at the end); a GME track plays as long
//   as the emulator produces it.
// - ZCMUSIC::position counts zcmusic_poll() calls while playing, from the
//   25 ms timer installed by zcmusic_init() and from the game's own calls;
//   the Triforce cutscene (link.cpp) waits for position >= 800.
// - zcmusic_get_tracks() / zcmusic_change_track() ask the emulator: the
//   track count of the NSF/GBS header, 1 for SPC, VGM and GYM. That decides
//   the track the game selects and remembers in ZCMUSIC::track.
//   zcmusic_play() starts the emulator at track 0 and leaves ZCMUSIC::track
//   as it was.
// - The play/pause/stop state machine, including its quirks: stopping an
//   OGG or MP3 leaves it "playing" (only the stream is gone, so it cannot be
//   restarted), stopping a tracker module that never played does nothing.
//   zcmusic_play() fails for OGG, MP3 and modules when the audio stream
//   cannot be created (no sound driver); a GME file then "plays" silently.
//
// Threading: zcmusic_poll() runs on the game thread and on the timer thread;
// the decoders and streams of playing files are only used with the playlist
// lock held, as in src/zcmusic.cpp.
//
// Written for zcport; the behaviour follows the game's own src/zcmusic.cpp.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <vector>

#include "zc_alleg.h"
#include "zcmusic.h"
#include "mutex.h"

#include "zcmusic_decoders.h"
#include "zcmusic_port.h"

#ifdef _MSC_VER
#define stricmp _stricmp
#endif

namespace dec = zcmusic_decoders;

int zcmusic_bufsz = 64;                 // KB, set from zc.cfg before zcmusic_init()
static int zcmusic_bufsz_private = 64;  // the value zcmusic_init() saw

zcmusic_port_buffer_hook_t zcmusic_port_buffer_hook = NULL;

namespace
{

const int kDuhChannels = 2;     // src/zcmusic.cpp's DUH_CHANNELS (stereo)
const int kDuhSamples = 44100;  // DUH_SAMPLES: the DUMB and GME output rate (Hz)

// One loaded file. The ZCMUSIC part is what the game reads.
struct PortMusic : public ZCMUSICBASE
{
    // ---- OGG, MP3 (src/zcmusic.cpp's OGGFILE, MP3FILE) ----
    PACKFILE *f;                    // the file while the first pass reads it
    std::vector<unsigned char> kept; // the bytes handed to the decoder so far
    bool kept_all;                  // 'kept' reaches the end of the file
    size_t replay;                  // after a restart: next byte of 'kept'
    dec::BlockDecoder *decoder;     // the alogg/almp3 stream: load until stop
    int vol;

    // ---- tracker modules (DUHFILE) ----
    dec::Module *duh;
    bool duh_player;                // al_start_duh()'s player exists: play until stop
    dec::ModulePlayer *sr;          // its sigrenderer, NULL once the module ended
    bool duh_running;               // not paused (ADP_PLAYING)
    float duh_volume;
    int silentcount;

    // ---- Game_Music_Emu (GMEFILE) ----
    dec::Emu *emu;
    int samples;

    // ---- the audio stream (from play until stop) ----
    AUDIOSTREAM *stream;
    int stream_len;                 // samples per buffer
    int stream_stereo;
    int stream_freq;

    // ---- only 2.55 and 3.0 music uses these (zcmusic_port.h) ----
    long frames_done;               // frames decoded, from the start of the file (OGG, MP3, modules)
    long frames_total;              // frames decoded, never rewinding
    long wrap_total;                // frames_total at the last jump in the file
    long wrap_from, wrap_to;        // the file positions of that jump
    long loop_start, loop_end;      // frames; loop_end 0: no loop points
    int speed;                      // 10000 is normal
    bool voice_volume;              // the module's volume is the voice's, not rendered in
    int fadevolume;
    int fadeoutframes;
    bool fresh_stream;              // no buffer handed to the stream yet
};

mutex playlist_mutex;
bool playlist_mutex_ready = false;
std::vector<ZCMUSIC*> playlist;         // files that are playing or paused
int libflags = 0;
bool autopoll_installed = false;

void lock()
{
    if(playlist_mutex_ready)
        mutex_lock(&playlist_mutex);
}

void unlock()
{
    if(playlist_mutex_ready)
        mutex_unlock(&playlist_mutex);
}

// The file name part of 'path' (after the last '/' or '\'), at most 255
// characters, as ZCMUSIC::filename holds it.
void set_music_name(ZCMUSIC *music, const char *path)
{
    const char *name = path;

    for(const char *p = path; *p; ++p)
    {
        if(*p == '/' || *p == '\\')
            name = p + 1;
    }

    strncpy(music->filename, name, 255);
    music->filename[255] = '\0';
}

PortMusic *new_music(const char *filename, int type)
{
    PortMusic *p = new PortMusic;
    p->type = type;
    p->playing = ZCM_STOPPED;
    p->position = 0;
    p->track = 0;
    set_music_name(p, filename);

    p->f = NULL;
    p->kept_all = false;
    p->replay = 0;
    p->decoder = NULL;
    p->vol = 0;
    p->duh = NULL;
    p->duh_player = false;
    p->sr = NULL;
    p->duh_running = false;
    p->duh_volume = 0;
    p->silentcount = 0;
    p->emu = NULL;
    p->samples = 0;
    p->stream = NULL;
    p->stream_len = 0;
    p->stream_stereo = 0;
    p->stream_freq = 0;
    p->frames_done = 0;
    p->frames_total = 0;
    p->wrap_total = 0;
    p->wrap_from = 0;
    p->wrap_to = 0;
    p->loop_start = 0;
    p->loop_end = 0;
    p->speed = 10000;
    p->voice_volume = false;
    p->fadevolume = 10000;
    p->fadeoutframes = 0;
    p->fresh_stream = false;
    return p;
}

void remove_from_playlist(ZCMUSIC *zcm)
{
    std::vector<ZCMUSIC*>::iterator b = playlist.begin();

    while(b != playlist.end())
    {
        if(*b == zcm)
            b = playlist.erase(b);
        else
            ++b;
    }
}

// ---- audio streams ---------------------------------------------------------------

bool open_stream(PortMusic *p, int len, int stereo, int freq, int vol)
{
    p->stream = play_audio_stream(len, 16, stereo, freq, vol, 128);

    if(!p->stream)
        return false;

    p->stream_len = len;
    p->stream_stereo = stereo;
    p->stream_freq = freq;
    p->fresh_stream = true;
    return true;
}

// As alogg/almp3 destroy their stream: the voice stops, the stream goes.
void end_stream(PortMusic *p)
{
    if(!p->stream)
        return;

    voice_stop(p->stream->voice);
    stop_audio_stream(p->stream);
    p->stream = NULL;
}

int stream_channels(const PortMusic *p)
{
    return p->stream_stereo ? 2 : 1;
}

// Allegro's streams take unsigned samples.
void to_unsigned(void *buf, long count)
{
    unsigned short *q = (unsigned short *)buf;

    for(long i = 0; i < count; i++)
        q[i] ^= 0x8000;
}

// Hands a filled buffer back to the stream.
void give_buffer(PortMusic *p, void *buf)
{
    if(zcmusic_port_buffer_hook)
        zcmusic_port_buffer_hook(p, buf, p->stream_len, p->stream_stereo, p->stream_freq);

    free_audio_stream_buffer(p->stream);
    p->fresh_stream = false;
}

// A jump in the file (a loop or a seek): for the position calculation.
void note_wrap(PortMusic *p, long from, long to)
{
    p->wrap_total = p->frames_total;
    p->wrap_from = from;
    p->wrap_to = to;
    p->frames_done = to;
}

// ---- OGG and MP3 ------------------------------------------------------------------

// The block size of src/zcmusic.cpp's reads.
long block_size()
{
    return (long)zcmusic_bufsz_private * 512;
}

dec::BlockDecoder *start_decoder(int type, const unsigned char *data, long len, bool last)
{
    return (type == ZCMF_OGG) ? dec::open_ogg(data, len, last) : dec::open_mp3(data, len, last);
}

// The decoder is set up from 'data' (the block src/zcmusic.cpp creates its
// stream from), 'f' then delivers the following blocks.
bool start_block_music(PortMusic *p, PACKFILE *f, std::vector<unsigned char> &data, long len)
{
    const long block = block_size();
    dec::BlockDecoder *d = start_decoder(p->type, &data[0], len, len < block);

    if(!d)
        return false;

    data.resize(len);
    p->kept.swap(data);
    p->kept_all = len < block;
    p->replay = p->kept.size();
    p->decoder = d;
    p->f = f;

    if(p->kept_all)
    {
        pack_fclose(p->f);
        p->f = NULL;
    }

    return true;
}

// src/zcmusic.cpp's load_ogg_file(): the first block of the file.
bool load_ogg(PortMusic *p, const char *filename)
{
    const long block = block_size();
    PACKFILE *f = pack_fopen_password(filename, F_READ, "");

    if(!f)
        return false;

    std::vector<unsigned char> data(block);
    long len = pack_fread(&data[0], block, f);

    if(len <= 0 || !start_block_music(p, f, data, len))
    {
        pack_fclose(f);
        return false;
    }

    return true;
}

// src/zcmusic.cpp's load_mp3_file(), read for read: 10 bytes, the ID3 tag
// skipped (relative to the 10 bytes read), a block that is dropped, then the
// block the stream is created from.
bool load_mp3(PortMusic *p, const char *filename)
{
    const long block = block_size();
    PACKFILE *f = pack_fopen_password(filename, F_READ, "");

    if(!f)
        return false;

    std::vector<unsigned char> data(block > 10 ? block : 10);
    bool ok = pack_fread(&data[0], 10, f) > 0;

    if(ok)
    {
        if(strncmp((const char *)&data[0], "ID3", 3) == 0)
        {
            int id3Size = ((data[6]&0x7F)<<21) | ((data[7]&0x7F)<<14) | ((data[8]&0x7F)<<7) | (data[9]&0x7F);
            pack_fseek(f, id3Size - 10);
        }

        ok = pack_fread(&data[0], block, f) > 0;
    }

    long len = 0;

    if(ok)
    {
        len = pack_fread(&data[0], block, f);
        ok = len > 0 && start_block_music(p, f, data, len);
    }

    if(!ok)
    {
        pack_fclose(f);
        return false;
    }

    return true;
}

void close_block_file(PortMusic *p)
{
    if(p->f)
    {
        pack_fclose(p->f);
        p->f = NULL;
    }
}

// Reads the file's next block into 'kept'. Returns its length.
long read_block(PortMusic *p)
{
    const long block = block_size();
    size_t old = p->kept.size();
    p->kept.resize(old + block);
    long got = pack_fread(&p->kept[old], block, p->f);

    if(got < 0)
        got = 0;

    p->kept.resize(old + got);

    if(got < block)
    {
        p->kept_all = true;
        close_block_file(p);
    }

    return got;
}

// The decoder asks for the next block: from the file on the first pass,
// from 'kept' after a restart (in the same blocks).
void feed_block(PortMusic *p)
{
    const long block = block_size();

    if(p->f)
    {
        size_t at = p->kept.size();
        long got = read_block(p);
        p->decoder->feed(got > 0 ? &p->kept[at] : NULL, got, got < block);
        p->replay = p->kept.size();
        return;
    }

    long left = (long)(p->kept.size() - p->replay);
    long n = left < block ? left : block;
    p->decoder->feed(n > 0 ? &p->kept[p->replay] : NULL, n, n < block);
    p->replay += n;
}

// The end of the file: src/zcmusic.cpp's ogg_reset()/mp3_reset() open the
// file again and create a new stream; the decoder starts again on the same
// bytes instead (the rest of the file is read first if the decoder stopped
// early).
bool restart_decoder(PortMusic *p)
{
    if(p->decoder && p->decoder->seekable())
    {
        const long to = p->loop_end > 0 ? p->loop_start : 0;
        note_wrap(p, p->frames_done, to);
        return p->decoder->seek(to);
    }

    note_wrap(p, p->frames_done, 0);
    delete p->decoder;
    p->decoder = NULL;

    while(p->f)
        read_block(p);

    const long block = block_size();
    long first = (long)p->kept.size() < block ? (long)p->kept.size() : block;

    if(first <= 0)
        return false;

    p->decoder = start_decoder(p->type, &p->kept[0], first, first < block);
    p->replay = first;

    if(p->decoder && p->decoder->channels() != stream_channels(p))
    {
        delete p->decoder;
        p->decoder = NULL;
    }

    return p->decoder != NULL;
}

// src/zcmusic.cpp's ogg_stop()/mp3_stop() and the unload functions: file,
// stream and decoder go.
void drop_block_music(PortMusic *p)
{
    close_block_file(p);
    end_stream(p);
    delete p->decoder;
    p->decoder = NULL;
    std::vector<unsigned char>().swap(p->kept);
    p->kept_all = false;
    p->replay = 0;
}

// One stream buffer of 'frames' frames, signed.
void decode_block_music(PortMusic *p, short *out, long frames)
{
    const int ch = stream_channels(p);
    long done = 0;
    long restarted_at = -1;

    while(done < frames && p->decoder)
    {
        long want = frames - done;

        if(p->loop_end > 0 && p->decoder->seekable())
        {
            long left = p->loop_end - p->frames_done;

            if(left <= 0)
            {
                note_wrap(p, p->frames_done, p->loop_start);

                if(!p->decoder->seek(p->loop_start))
                    p->loop_end = 0; // can't go there: play on to the end instead

                continue;
            }

            if(want > left)
                want = left;
        }

        const long got = p->decoder->decode(out + done * ch, want);
        done += got;
        p->frames_done += got;
        p->frames_total += got;

        if(got == want)
            continue;

        if(p->decoder->hungry())
        {
            feed_block(p);
            continue;
        }

        if(p->decoder->finished())
        {
            if(restarted_at == done)
                break; // nothing to play in the file: silence until the next buffer

            restarted_at = done;
            restart_decoder(p);
            continue;
        }

        break;
    }

    if(done < frames)
        memset(out + done * ch, 0, (size_t)(frames - done) * ch * sizeof(short));
}

void poll_block_music(PortMusic *p)
{
    if(!p->decoder || !p->stream)
        return;

    void *buf = get_audio_stream_buffer(p->stream);

    if(!buf)
        return;

    decode_block_music(p, (short *)buf, p->stream_len);
    to_unsigned(buf, (long)p->stream_len * stream_channels(p));
    give_buffer(p, buf);

    // The file could not be started again: as when src/zcmusic.cpp's reset
    // fails, the stream is gone and the file stays "playing".
    if(!p->decoder)
        drop_block_music(p);
}

// ---- tracker modules -----------------------------------------------------------

// The whole file, read the way DUMB's packfile reader (dumb_register_packfiles())
// opens it: pack_fopen() with the current password.
bool read_packfile(const char *filename, std::vector<unsigned char> &out)
{
    PACKFILE *f = pack_fopen(filename, F_READ);

    if(!f)
        return false;

    const long chunk = 64 * 1024;

    for(;;)
    {
        size_t old = out.size();
        out.resize(old + chunk);
        long got = pack_fread(&out[old], chunk, f);

        if(got < 0)
            got = 0;

        out.resize(old + got);

        if(got < chunk)
            break;
    }

    pack_fclose(f);
    return true;
}

dec::Module *load_module(const char *filename, dec::ModuleKind kind)
{
    std::vector<unsigned char> data;

    if(!read_packfile(filename, data) || data.empty())
        return NULL;

    return dec::module_load(&data[0], (long)data.size(), kind);
}

// al_start_duh(duh, DUH_CHANNELS, 0, vol / 255, zcmusic_bufsz * 1024, DUH_SAMPLES).
bool start_module(PortMusic *p, int vol)
{
    const int len = zcmusic_bufsz_private * 1024;

    if(!open_stream(p, len, kDuhChannels == 2, kDuhSamples, 255))
        return false;

    voice_set_priority(p->stream->voice, 255);
    p->sr = dec::module_start(p->duh, kDuhChannels, 0);

    if(!p->sr)
    {
        stop_audio_stream(p->stream);
        p->stream = NULL;
        return false;
    }

    p->duh_player = true;
    p->duh_running = true;
    p->duh_volume = (float)vol / (float)255;
    p->silentcount = 0;
    return true;
}

// Starts the module's sound again at frame 'to' of the file.
void seek_module(PortMusic *p, long to)
{
    note_wrap(p, p->frames_done, to);
    dec::ModulePlayer *sr = dec::module_start(p->duh, kDuhChannels, (int32_t)(((int64_t)to * 65536) / p->stream_freq));

    if(sr)
    {
        dec::module_stop(p->sr);
        p->sr = sr;
    }
}

// One stream buffer, stopping at the loop point if the module has one.
long render_module(PortMusic *p, void *buf)
{
    if(p->loop_end <= 0)
    {
        const long n = dec::module_render(p->sr, p->duh_volume, p->stream_freq, p->stream_len, buf);

        if(n > 0)
        {
            p->frames_done += n;
            p->frames_total += n;
        }

        return n;
    }

    const int channels = stream_channels(p);
    long done = 0;

    while(done < p->stream_len)
    {
        long left = p->loop_end - p->frames_done;

        if(left <= 0)
        {
            seek_module(p, p->loop_start);
            left = p->loop_end - p->frames_done;
        }

        const long want = (p->stream_len - done) < left ? (p->stream_len - done) : left;
        const long got = dec::module_render(p->sr, p->duh_volume, p->stream_freq, want, (unsigned short *)buf + done * channels);

        if(got <= 0)
            break;

        done += got;
        p->frames_done += got;
        p->frames_total += got;
    }

    return done;
}

// al_poll_duh().
void poll_module(PortMusic *p)
{
    if(!p->duh_player || !p->sr || !p->duh_running || !p->stream)
        return;

    void *buf = get_audio_stream_buffer(p->stream);

    if(!buf)
        return;

    long n = render_module(p, buf);

    if(n == 0 && ++p->silentcount >= 2)
    {
        // The module has ended: the player stops (it stays "playing").
        dec::module_stop(p->sr);
        p->sr = NULL;
        free_audio_stream_buffer(p->stream);
        stop_audio_stream(p->stream);
        p->stream = NULL;
        return;
    }

    if(n < 0)
        n = 0;

    unsigned short *q = (unsigned short *)buf;
    const long total = (long)p->stream_len * stream_channels(p);

    for(long i = n * stream_channels(p); i < total; i++)
        q[i] = 0x8000;

    give_buffer(p, buf);
}

// al_stop_duh().
void stop_module(PortMusic *p)
{
    if(p->sr)
    {
        dec::module_stop(p->sr);
        p->sr = NULL;
        stop_audio_stream(p->stream);
    }

    p->stream = NULL;
    p->duh_player = false;
}

// ---- Game_Music_Emu --------------------------------------------------------------

// src/zcmusic.cpp's gme_play().
void start_gme(PortMusic *p, int vol)
{
    dec::emu_start_track(p->emu, 0);
    int samples = 512;
    int buf_size = 2 * kDuhSamples / 50;

    while(samples < buf_size)
        samples *= 2;

    p->samples = samples;

    if(p->stream)
    {
        stop_audio_stream(p->stream);
        p->stream = NULL;
    }

    // As in src/zcmusic.cpp, playing goes on without a stream if none can be
    // created (the reference then crashes on its next poll; here it is silent).
    open_stream(p, samples, TRUE, kDuhSamples, vol);
}

// src/zcmusic.cpp's poll_gme_file().
void poll_gme(PortMusic *p)
{
    if(!p->emu || !p->stream)
        return;

    void *buf = get_audio_stream_buffer(p->stream);

    if(!buf)
        return;

    dec::emu_play(p->emu, 2L * p->samples, (short *)buf);
    to_unsigned(buf, 2L * p->samples);
    give_buffer(p, buf);
}

// Frees everything a file holds (it is no longer in the playlist).
void destroy_music(PortMusic *p)
{
    if(p->duh_player)
        stop_module(p);

    drop_block_music(p); // also ends a GME file's stream

    dec::module_unload(p->duh);
    p->duh = NULL;
    dec::emu_unload(p->emu);
    p->emu = NULL;
    delete p;
}

} // namespace

extern "C"
{

static void zcmusic_autopoll()
{
    zcmusic_poll();
}

bool zcmusic_init(int flags)                              /* = -1 */
{
    zcmusic_bufsz_private = zcmusic_bufsz;

    if(flags & ZCMF_DUH)
    {
        dec::module_init();
        libflags |= ZCMF_DUH;
    }

    libflags |= flags & (ZCMF_OGG | ZCMF_MP3 | ZCMF_GME);

    if(!playlist_mutex_ready)
    {
        mutex_init(&playlist_mutex);
        playlist_mutex_ready = true;
    }

    // As src/zcmusic.cpp: poll every 25 ms (this advances 'position').
    install_int_ex(zcmusic_autopoll, MSEC_TO_TIMER(25));
    autopoll_installed = true;
    return true;
}

bool zcmusic_poll(int flags)                              /* = -1 */
{
    lock();
    std::vector<ZCMUSIC*>::iterator b = playlist.begin();

    while(b != playlist.end())
    {
        if((*b)->playing == ZCM_STOPPED)
        {
            b = playlist.erase(b);
            continue;
        }

        if((*b)->playing == ZCM_PLAYING)
        {
            (*b)->position++;
            PortMusic *p = static_cast<PortMusic*>(*b);

            switch((*b)->type & flags & libflags)          // only poll those specified by 'flags'
            {
            case ZCMF_DUH:
                poll_module(p);
                break;

            case ZCMF_OGG:
            case ZCMF_MP3:
                poll_block_music(p);
                break;

            case ZCMF_GME:
                poll_gme(p);
                break;
            }
        }

        ++b;
    }

    unlock();
    return true;
}

void zcmusic_exit()
{
    lock();

    while(!playlist.empty())
    {
        ZCMUSIC *zcm = playlist.back();
        playlist.pop_back();
        unlock();
        zcmusic_unload_file(zcm);
        lock();
    }

    unlock();

    if(autopoll_installed)
    {
        remove_int(zcmusic_autopoll);
        autopoll_installed = false;
    }

    if(libflags & ZCMF_DUH)
        dec::module_exit();

    libflags = 0;
}

ZCMUSIC const * zcmusic_load_file(char *filename)
{
    if(filename == NULL)
        return NULL;

    al_trace("Trying to load %s\n", filename);

    if(strlen(filename) > 255)
    {
        al_trace("Music file '%s' not loaded: filename too long\n", filename);
        return NULL;
    }

    char *ext = get_extension(filename);

    if(stricmp(ext, "ogg") == 0 && (libflags & ZCMF_OGG))
    {
        PortMusic *p = new_music(filename, ZCMF_OGG);

        if(!load_ogg(p, filename))
        {
            destroy_music(p);
            al_trace("OGG file '%s' not loaded.\n", filename);
            return NULL;
        }

        return p;
    }

    if(stricmp(ext, "mp3") == 0 && (libflags & ZCMF_MP3))
    {
        PortMusic *p = new_music(filename, ZCMF_MP3);

        if(!load_mp3(p, filename))
        {
            destroy_music(p);
            al_trace("MP3 file '%s' not found.\n", filename);
            return NULL;
        }

        return p;
    }

    if(libflags & ZCMF_DUH)
    {
        static const struct
        {
            const char *ext, *name;
            dec::ModuleKind kind;
        } kinds[4] =
        {
            { "it", "IT", dec::MODULE_IT }, { "xm", "XM", dec::MODULE_XM },
            { "s3m", "S3M", dec::MODULE_S3M }, { "mod", "MOD", dec::MODULE_MOD }
        };

        for(int i = 0; i < 4; i++)
        {
            if(stricmp(ext, kinds[i].ext) != 0)
                continue;

            dec::Module *m = load_module(filename, kinds[i].kind);

            if(m)
            {
                PortMusic *p = new_music(filename, ZCMF_DUH);
                p->duh = m;
                return p;
            }

            al_trace("%s file '%s' not found.\n", kinds[i].name, filename);
            break;
        }
    }

    if(libflags & ZCMF_GME)
    {
        if(stricmp(ext, "spc") == 0 || stricmp(ext, "gbs") == 0 || stricmp(ext, "vgm") == 0
           || stricmp(ext, "gym") == 0 || stricmp(ext, "nsf") == 0)
        {
            dec::Emu *emu = dec::emu_load(filename, ext, kDuhSamples);

            if(emu)
            {
                PortMusic *p = new_music(filename, ZCMF_GME);
                p->emu = emu;
                return p;
            }

            al_trace("%s file '%s' not found.\n", ext, filename);
        }
    }

    return NULL;
}

bool zcmusic_play(ZCMUSIC* zcm, int vol)
{
    if(zcm == NULL)
        return FALSE;

    PortMusic *p = static_cast<PortMusic*>(zcm);

    if(zcm->playing != ZCM_STOPPED)
    {
        // Adjust the volume (src/zcmusic.cpp does this without the lock).
        lock();

        switch(zcm->type & libflags)
        {
        case ZCMF_DUH:
            if(p->duh_player && p->voice_volume && p->stream)
                voice_set_volume(p->stream->voice, vol);
            else if(p->duh_player)
                p->duh_volume = (float)vol / (float)255;

            break;

        case ZCMF_OGG:
        case ZCMF_MP3:
            if(p->decoder)
            {
                if(p->stream)
                {
                    voice_set_volume(p->stream->voice, vol);
                    voice_set_pan(p->stream->voice, 128);
                }

                p->vol = vol;
            }

            break;

        case ZCMF_GME:
            // src/zcmusic.cpp: "need to figure out volume switch"
            break;
        }

        unlock();
        return TRUE;
    }

    bool ok = true;

    switch(zcm->type & libflags)
    {
    case ZCMF_DUH:
        if(p->duh)
            ok = start_module(p, vol);

        break;

    case ZCMF_OGG:
    case ZCMF_MP3:
        if(p->decoder)                   // no decoder after zcmusic_stop()
        {
            end_stream(p);
            const int ch = p->decoder->channels();
            ok = open_stream(p, (zcmusic_bufsz_private * 1024) / (2 * ch), ch == 2, p->decoder->rate(), vol);
        }
        else
            ok = false;

        p->vol = vol;
        break;

    case ZCMF_GME:
        start_gme(p, vol);
        break;
    }

    if(ok)
    {
        lock();
        zcm->position = 0;
        zcm->playing = ZCM_PLAYING;
        playlist.push_back(zcm);
        unlock();
    }

    return ok;
}

bool zcmusic_pause(ZCMUSIC* zcm, int pause)               /* = -1 */
{
    if(zcm == NULL)
        return FALSE;

    PortMusic *p = static_cast<PortMusic*>(zcm);
    lock();

    if(zcm->playing != ZCM_STOPPED)
    {
        int state = ZCM_PLAYING;

        switch(pause)
        {
        case ZCM_TOGGLE:
            state = (zcm->playing == ZCM_PAUSED) ? ZCM_PLAYING : ZCM_PAUSED;
            break;

        case ZCM_RESUME:
            state = ZCM_PLAYING;
            break;

        case ZCM_PAUSE:
            state = ZCM_PAUSED;
            break;
        }

        if(state != zcm->playing)                        // if the state has actually changed
        {
            zcm->playing = state;

            switch(zcm->type & libflags)
            {
            case ZCMF_DUH:
                // al_pause_duh() / al_resume_duh()
                if(p->duh_player && p->sr && p->stream)
                {
                    if(state == ZCM_PAUSED && p->duh_running)
                    {
                        voice_stop(p->stream->voice);
                        p->duh_running = false;
                    }
                    else if(state == ZCM_PLAYING && !p->duh_running)
                    {
                        voice_start(p->stream->voice);
                        p->duh_running = true;
                    }
                }

                break;

            case ZCMF_OGG:
            case ZCMF_MP3:
            case ZCMF_GME:
                if(p->stream)
                {
                    if(state == ZCM_PAUSED)
                        voice_stop(p->stream->voice);
                    else
                        voice_start(p->stream->voice);
                }

                break;
            }
        }
    }

    unlock();
    return TRUE;
}

bool zcmusic_stop(ZCMUSIC* zcm)
{
    if(zcm == NULL)
        return FALSE;

    PortMusic *p = static_cast<PortMusic*>(zcm);
    lock();

    switch(zcm->type & libflags)
    {
    case ZCMF_DUH:
        if(p->duh_player)
        {
            stop_module(p);
            zcm->playing = ZCM_STOPPED;
        }

        break;

    case ZCMF_OGG:
    case ZCMF_MP3:
        drop_block_music(p); // 'playing' stays as it is (src/zcmusic.cpp)
        break;

    case ZCMF_GME:
        if(zcm->playing != ZCM_STOPPED && p->stream)
        {
            stop_audio_stream(p->stream);
            p->stream = NULL;
        }

        zcm->playing = ZCM_STOPPED;
        break;
    }

    unlock();
    return TRUE;
}

void zcmusic_unload_file(ZCMUSIC* &zcm)
{
    if(zcm == NULL)
        return;

    lock();
    remove_from_playlist(zcm);
    unlock();

    destroy_music(static_cast<PortMusic*>(zcm));
    zcm = NULL;
}

int zcmusic_get_tracks(ZCMUSIC* zcm)
{
    if(zcm == NULL)
        return 0;

    if((zcm->type & libflags) != ZCMF_GME)
        return 0;

    int t = dec::emu_track_count(static_cast<PortMusic*>(zcm)->emu);
    return (t > 1) ? t : 0;
}

int zcmusic_change_track(ZCMUSIC* zcm, int tracknum)
{
    if(zcm == NULL)
        return -1;

    switch(zcm->type & libflags)
    {
    case ZCMF_DUH:
    case ZCMF_OGG:
    case ZCMF_MP3:
        return -1;

    case ZCMF_GME:
    {
        PortMusic *p = static_cast<PortMusic*>(zcm);
        lock();
        int t = dec::emu_track_count(p->emu);

        if(tracknum < 0 || tracknum >= t)
            tracknum = 0;

        dec::emu_start_track(p->emu, tracknum);
        zcm->track = tracknum;
        unlock();
        return tracknum;
    }
    }

    return 0;
}

} // extern "C"

// ---- music of 2.55 and 3.0 quests ---------------------------------------------------------

namespace
{

// A whole OGG or MP3, decoded from its start.
bool load_whole(PortMusic *p, const char *filename, bool ogg)
{
    std::vector<unsigned char> data;
    PACKFILE *f = pack_fopen_password(filename, F_READ, "");

    if(!f)
        return false;

    const long chunk = 256 * 1024;

    for(;;)
    {
        size_t old = data.size();
        data.resize(old + chunk);
        long got = pack_fread(&data[old], chunk, f);

        if(got < 0)
            got = 0;

        data.resize(old + got);

        if(got < chunk)
            break;
    }

    pack_fclose(f);

    if(data.empty())
        return false;

    // An MP3's ID3 tag can hold enough binary data to look like frames to the decoder.
    size_t skip = 0;

    if(!ogg && data.size() > 10 && memcmp(&data[0], "ID3", 3) == 0)
    {
        skip = (size_t)(((data[6] & 0x7F) << 21) | ((data[7] & 0x7F) << 14) | ((data[8] & 0x7F) << 7) | (data[9] & 0x7F)) + 10;

        if(skip >= data.size())
            skip = 0;
    }

    p->kept.assign(data.begin() + skip, data.end());
    p->kept_all = true;
    p->replay = p->kept.size();
    p->decoder = ogg ? dec::open_ogg_seekable(&p->kept[0], (int32_t)p->kept.size()) : dec::open_mp3_seekable(&p->kept[0], (int32_t)p->kept.size());
    return p->decoder != NULL;
}

// Whole-file decoders are needed from here on (a file started by 2.53's path, then asked
// for its position).
bool make_seekable(PortMusic *p)
{
    if(!p->decoder)
        return false;

    if(p->decoder->seekable())
        return true;

    while(p->f)
        read_block(p);

    if(p->kept.empty())
        return false;

    dec::BlockDecoder *d = (p->type == ZCMF_OGG) ? dec::open_ogg_seekable(&p->kept[0], (int32_t)p->kept.size()) : dec::open_mp3_seekable(&p->kept[0], (int32_t)p->kept.size());

    if(!d)
        return false;

    if(d->channels() != p->decoder->channels() || d->rate() != p->decoder->rate() || !d->seek((int32_t)p->frames_done))
    {
        delete d;
        return false;
    }

    delete p->decoder;
    p->decoder = d;
    return true;
}

// What is buffered ahead of what is being heard, frames. The ring of buffers is what
// zcmusic_poll() has handed over; the voice's position says how far it has played.
long queued_frames(PortMusic *p)
{
    if(!p->stream || p->fresh_stream)
        return 0;

    const long ring = (long)p->stream->bufcount * 2 * p->stream_len;
    const int pos = voice_get_position(p->stream->voice);

    if(ring <= 0 || pos < 0)
        return 0;

    const long written = ((long)p->stream->bufnum + 1) * p->stream_len;
    long queued = ((written - pos) % ring + ring) % ring;
    return queued == 0 ? ring : queued;
}

// The position being heard, frames from the start of the file.
long heard_frames(PortMusic *p)
{
    if(p->fresh_stream)
        return p->frames_done;

    const long heard = p->frames_total - queued_frames(p);

    if(heard < 0)
        return 0;

    if(heard < p->wrap_total)
        return p->wrap_from - (p->wrap_total - heard);

    return p->wrap_to + (heard - p->wrap_total);
}

// Throws away what is buffered so a seek takes effect straight away.
void flush_stream(PortMusic *p)
{
    if(!p->stream)
        return;

    const int len = p->stream_len, stereo = p->stream_stereo, freq = p->stream_freq;
    voice_stop(p->stream->voice);
    stop_audio_stream(p->stream);
    p->stream = NULL;
    open_stream(p, len, stereo, freq, p->vol);

    if(!p->stream)
        return;

    if(p->speed != 10000)
        voice_set_frequency(p->stream->voice, (int)((int64_t)freq * p->speed / 10000));

    if(p->playing == ZCM_PAUSED)
        voice_stop(p->stream->voice);
}

long secs_to_frames(double secs, int rate)
{
    return (long)(secs * rate);
}

int current_rate(PortMusic *p)
{
    if(p->type == ZCMF_DUH)
        return kDuhSamples;

    return p->decoder ? p->decoder->rate() : 0;
}

long length_frames(PortMusic *p)
{
    if(p->type == ZCMF_DUH)
        return p->duh ? (long)(((int64_t)dec::module_length(p->duh) * kDuhSamples) / 65536) : 0;

    if(!make_seekable(p))
        return 0;

    return p->decoder->total_frames();
}

} // namespace

extern "C"
{

ZCMUSIC *zcmusic_port_load_whole(const char *filename)
{
    char *ext = get_extension(filename);

    if((stricmp(ext, "ogg") == 0 && (libflags & ZCMF_OGG)) || (stricmp(ext, "mp3") == 0 && (libflags & ZCMF_MP3)))
    {
        const bool ogg = stricmp(ext, "ogg") == 0;
        PortMusic *p = new_music(filename, ogg ? ZCMF_OGG : ZCMF_MP3);

        if(!load_whole(p, filename, ogg))
        {
            destroy_music(p);
            al_trace("Music file '%s' not loaded.\n", filename);
            return NULL;
        }

        return p;
    }

    return (ZCMUSIC *)zcmusic_load_file(const_cast<char *>(filename));
}

bool zcmusic_set_volume(ZCMUSIC *zcm, int vol)
{
    if(zcm == NULL)
        return FALSE;

    PortMusic *p = static_cast<PortMusic *>(zcm);
    lock();
    p->vol = vol;

    if(p->stream)
    {
        voice_set_volume(p->stream->voice, vol);

        if((p->type & libflags) == ZCMF_DUH)
        {
            p->voice_volume = true;
            p->duh_volume = 1.0f;
        }
    }

    unlock();
    return TRUE;
}

int zcmusic_get_curpos(ZCMUSIC *zcm)
{
    if(zcm == NULL)
        return -10000;

    PortMusic *p = static_cast<PortMusic *>(zcm);
    lock();
    int result = -10000;

    switch(zcm->type & libflags)
    {
    case ZCMF_OGG:
    case ZCMF_MP3:
    case ZCMF_DUH:
        if(p->stream && current_rate(p) > 0)
        {
            int64_t frames = heard_frames(p);

            if(frames < 0)
                frames = 0;

            result = (int)((frames * 10000) / current_rate(p));
        }

        break;
    }

    unlock();
    return result;
}

void zcmusic_set_curpos(ZCMUSIC *zcm, int value)
{
    if(zcm == NULL)
        return;

    PortMusic *p = static_cast<PortMusic *>(zcm);
    lock();

    switch(zcm->type & libflags)
    {
    case ZCMF_OGG:
    case ZCMF_MP3:
        if(p->stream && make_seekable(p))
        {
            const long to = secs_to_frames(value / 10000.0, p->decoder->rate());
            note_wrap(p, p->frames_done, to);
            p->decoder->seek((int32_t)to);
            flush_stream(p);
        }

        break;

    case ZCMF_DUH:
        if(p->sr && p->stream)
        {
            seek_module(p, secs_to_frames(value / 10000.0, kDuhSamples));
            flush_stream(p);
        }

        break;
    }

    unlock();
}

void zcmusic_set_speed(ZCMUSIC *zcm, int value)
{
    if(zcm == NULL || value <= 0)
        return;

    PortMusic *p = static_cast<PortMusic *>(zcm);
    lock();

    switch(zcm->type & libflags)
    {
    case ZCMF_OGG:
    case ZCMF_MP3:
    case ZCMF_DUH:
        if(p->stream)
        {
            p->speed = value;
            voice_set_frequency(p->stream->voice, (int)((int64_t)p->stream_freq * value / 10000));
        }

        break;
    }

    unlock();
}

int zcmusic_get_length(ZCMUSIC *zcm)
{
    if(zcm == NULL)
        return -10000;

    PortMusic *p = static_cast<PortMusic *>(zcm);
    lock();
    int result = -10000;

    switch(zcm->type & libflags)
    {
    case ZCMF_OGG:
    case ZCMF_MP3:
    case ZCMF_DUH:
        if(p->stream && current_rate(p) > 0)
        {
            int64_t frames = length_frames(p);

            if(frames > 0)
                result = (int)((frames * 10000) / current_rate(p));
        }

        break;
    }

    unlock();
    return result;
}

void zcmusic_set_loop(ZCMUSIC *zcm, double start, double end)
{
    if(zcm == NULL)
        return;

    if(start == 0.0 && end == 0.0)
        return;

    PortMusic *p = static_cast<PortMusic *>(zcm);
    lock();

    switch(zcm->type & libflags)
    {
    case ZCMF_OGG:
    case ZCMF_MP3:
        if(!make_seekable(p))
            break;

        // fall through
    case ZCMF_DUH:
        if(p->stream && current_rate(p) > 0)
        {
            const int rate = current_rate(p);
            const long first = secs_to_frames(start, rate);
            long last = secs_to_frames(end, rate);

            if(end < start)
                last = length_frames(p);

            if(last > first && first >= 0)
            {
                p->loop_start = first;
                p->loop_end = last;
            }
        }

        break;
    }

    unlock();
}

int zcmusic_port_fadevolume(const ZCMUSIC *zcm)
{
    return zcm ? static_cast<const PortMusic *>(zcm)->fadevolume : 10000;
}

void zcmusic_port_set_fadevolume(ZCMUSIC *zcm, int v)
{
    if(zcm)
        static_cast<PortMusic *>(zcm)->fadevolume = v;
}

int zcmusic_port_fadeoutframes(const ZCMUSIC *zcm)
{
    return zcm ? static_cast<const PortMusic *>(zcm)->fadeoutframes : 0;
}

void zcmusic_port_set_fadeoutframes(ZCMUSIC *zcm, int v)
{
    if(zcm)
        static_cast<PortMusic *>(zcm)->fadeoutframes = v;
}

bool zcmusic_port_decode_ogg_sample(const void *data, long len, short **pcm, int *channels, int *rate, int *frames)
{
    int32_t f = 0;

    if(!data || len <= 0 || !dec::decode_ogg_sample(data, (int32_t)len, pcm, channels, rate, &f))
        return false;

    *frames = (int)f;
    return true;
}

} // extern "C"

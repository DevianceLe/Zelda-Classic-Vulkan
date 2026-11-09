// zplayer: the music decoders behind src/port/game/zcmusic_port.cpp, on the
// libraries under third_party/ (see zcmusic_decoders.h).
//
// Written for zcport (no Allegro code).

#include "zcmusic_decoders.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

// The one translation unit with minimp3's implementation.
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "dumb.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4100 4244 4512)
#endif
#include "gme/Nsf_Emu.h"
#include "gme/Gbs_Emu.h"
#include "gme/Spc_Emu.h"
#include "gme/Vgm_Emu.h"
#include "gme/Gym_Emu.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// The top-level CMakeLists puts the Allegro build's DUMB 0.9.3 and GME 0.3.0
// headers (include/dumb, include/gme) on every directory's include path;
// src/port removes them. Make sure the vendored ones are the ones used.
#if !defined(DUMB_MAJOR_VERSION) || DUMB_MAJOR_VERSION < 2
#error "zcmusic_decoders.cpp needs DUMB 2 (third_party/dumb), not include/dumb"
#endif
#if !defined(GME_VERSION) || GME_VERSION < 0x000605
#error "zcmusic_decoders.cpp needs Game_Music_Emu 0.6.5 (third_party/gme), not include/gme"
#endif
#if !defined(VGM_YM2612_GENS)
#error "zc_gme's VGM_YM2612_GENS must reach its users (the Vgm_Emu class layout depends on it)"
#endif

namespace zcmusic_decoders
{

namespace
{

short float_to_s16(float x)
{
    float s = x * 32768.0f;

    if(s >= 32767.0f)
        return 32767;

    if(s <= -32768.0f)
        return -32768;

    return (short)floorf(s + 0.5f);
}

bool same_ext(const char *a, const char *b)
{
    for(; *a && *b; ++a, ++b)
    {
        if(tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    }

    return *a == *b;
}

// Input bytes not yet decoded.
class InputQueue
{
public:
    InputQueue() : pos_(0) {}

    void assign(const void *data, long len)
    {
        const unsigned char *p = (const unsigned char *)data;
        buf_.assign(p, p + (len > 0 ? len : 0));
        pos_ = 0;
    }

    void append(const void *data, long len)
    {
        if(pos_ > 0)
        {
            buf_.erase(buf_.begin(), buf_.begin() + pos_);
            pos_ = 0;
        }

        if(len > 0)
        {
            const unsigned char *p = (const unsigned char *)data;
            buf_.insert(buf_.end(), p, p + len);
        }
    }

    const unsigned char *data() const { return buf_.empty() ? NULL : &buf_[0] + pos_; }
    long size() const { return (long)(buf_.size() - pos_); }
    void consume(long n) { pos_ += (size_t)n; }

private:
    std::vector<unsigned char> buf_;
    size_t pos_;
};

// Decoded frames not yet handed out (interleaved, the decoder's channels()).
class PcmQueue
{
public:
    PcmQueue() : pos_(0) {}

    bool empty() const { return pos_ >= buf_.size(); }

    std::vector<short> &refill()
    {
        buf_.clear();
        pos_ = 0;
        return buf_;
    }

    // Copies up to 'frames' frames to 'out'; returns the frames copied.
    long take(short *out, long frames, int channels)
    {
        long have = (long)((buf_.size() - pos_) / channels);
        long n = have < frames ? have : frames;
        memcpy(out, &buf_[pos_], (size_t)n * channels * sizeof(short));
        pos_ += (size_t)n * channels;
        return n;
    }

    // Skips up to 'frames' frames.
    void drop(long frames, int channels)
    {
        long have = (long)((buf_.size() - pos_) / channels);
        pos_ += (size_t)(have < frames ? have : frames) * channels;
    }

private:
    std::vector<short> buf_;
    size_t pos_;
};

// ---- OGG Vorbis -------------------------------------------------------------

class OggDecoder : public BlockDecoder
{
public:
    OggDecoder() : v_(NULL), eof_(false), need_(false), done_(false), channels_(0), out_channels_(0), rate_(0) {}

    ~OggDecoder()
    {
        if(v_)
            stb_vorbis_close(v_);
    }

    bool open(const void *data, long len, bool last)
    {
        in_.assign(data, len);
        eof_ = last;

        if(in_.size() <= 0)
            return false;

        // NULL also when the block ends before the headers do
        // (VORBIS_need_more_data): alogg only has this block when it opens
        // the stream.
        int used = 0, error = 0;
        v_ = stb_vorbis_open_pushdata(in_.data(), (int)in_.size(), &used, &error, NULL);

        if(!v_)
            return false;

        in_.consume(used);
        stb_vorbis_info info = stb_vorbis_get_info(v_);
        channels_ = info.channels;
        rate_ = (int)info.sample_rate;

        if(channels_ < 1 || rate_ <= 0)
            return false;

        // alogg plays mono or stereo; of more channels the first two are kept.
        out_channels_ = channels_ >= 2 ? 2 : 1;
        return true;
    }

    int channels() const { return out_channels_; }
    int rate() const { return rate_; }

    void feed(const void *data, int32_t len, bool last)
    {
        in_.append(data, len);

        if(last)
            eof_ = true;

        need_ = false;
    }

    bool hungry() const { return need_ && !eof_ && !done_; }
    bool finished() const { return done_; }

    int32_t decode(short *out, int32_t frames)
    {
        long n = 0;

        while(n < frames)
        {
            if(!pcm_.empty())
            {
                n += pcm_.take(out + n * out_channels_, frames - n, out_channels_);
                continue;
            }

            if(done_ || need_)
                break;

            int ch = 0, samples = 0;
            float **output = NULL;
            int used = 0;

            if(in_.size() > 0)
                used = stb_vorbis_decode_frame_pushdata(v_, in_.data(), (int)in_.size(), &ch, &output, &samples);

            if(used == 0 && samples == 0)
            {
                int error = in_.size() > 0 ? stb_vorbis_get_error(v_) : VORBIS_need_more_data;

                if(eof_ || (error != VORBIS_need_more_data && error != VORBIS__no_error))
                    done_ = true; // end of the file (or a stream it cannot go on with)
                else
                    need_ = true;

                continue;
            }

            in_.consume(used);

            if(samples <= 0 || !output)
                continue; // the first packet, or resynchronising

            std::vector<short> &pcm = pcm_.refill();
            pcm.resize((size_t)samples * out_channels_);

            for(int i = 0; i < samples; i++)
            {
                for(int c = 0; c < out_channels_; c++)
                    pcm[(size_t)i * out_channels_ + c] = float_to_s16(output[c < ch ? c : 0][i]);
            }
        }

        return n;
    }

private:
    stb_vorbis *v_;
    InputQueue in_;
    PcmQueue pcm_;
    bool eof_, need_, done_;
    int channels_, out_channels_, rate_;
};

// ---- MP3 --------------------------------------------------------------------

// minimp3 recognises a frame by the headers of the frames after it, so it is
// given at least this much data until the file has been read to its end.
const long kMp3MinInput = 16 * 1024;

class Mp3Decoder : public BlockDecoder
{
public:
    Mp3Decoder() : eof_(false), need_(false), done_(false), channels_(0), rate_(0)
    {
        mp3dec_init(&dec_);
    }

    // almp3 decodes the first frame of the first block to learn the format.
    bool open(const void *data, long len, bool last)
    {
        in_.assign(data, len);
        eof_ = last;

        for(;;)
        {
            if(in_.size() <= 0)
                return false;

            mp3dec_frame_info_t info;
            int samples = mp3dec_decode_frame(&dec_, in_.data(), (int)in_.size(), frame_, &info);

            if(info.frame_bytes <= 0)
                return false;

            in_.consume(info.frame_bytes);

            if(samples > 0 && info.channels >= 1 && info.hz > 0)
            {
                channels_ = info.channels >= 2 ? 2 : 1;
                rate_ = info.hz;
                store(samples, info.channels);
                return true;
            }
        }
    }

    int channels() const { return channels_; }
    int rate() const { return rate_; }

    void feed(const void *data, int32_t len, bool last)
    {
        in_.append(data, len);

        if(last)
            eof_ = true;

        need_ = false;
    }

    bool hungry() const { return need_ && !eof_ && !done_; }
    bool finished() const { return done_; }

    int32_t decode(short *out, int32_t frames)
    {
        long n = 0;

        while(n < frames)
        {
            if(!pcm_.empty())
            {
                n += pcm_.take(out + n * channels_, frames - n, channels_);
                continue;
            }

            if(done_ || need_)
                break;

            if(!eof_ && in_.size() < kMp3MinInput)
            {
                need_ = true;
                break;
            }

            if(in_.size() <= 0)
            {
                done_ = true;
                break;
            }

            mp3dec_frame_info_t info;
            int samples = mp3dec_decode_frame(&dec_, in_.data(), (int)in_.size(), frame_, &info);

            if(info.frame_bytes <= 0)
            {
                // Not a whole frame left.
                if(eof_)
                    done_ = true;
                else
                    need_ = true;

                continue;
            }

            in_.consume(info.frame_bytes);

            if(samples > 0)
                store(samples, info.channels);
        }

        return n;
    }

private:
    // One decoded frame into pcm_, in the decoder's channel count.
    void store(int samples, int frame_channels)
    {
        std::vector<short> &pcm = pcm_.refill();
        pcm.resize((size_t)samples * channels_);

        if(frame_channels == channels_)
        {
            memcpy(&pcm[0], frame_, (size_t)samples * channels_ * sizeof(short));
        }
        else if(channels_ == 2)
        {
            for(int i = 0; i < samples; i++)
                pcm[2 * i] = pcm[2 * i + 1] = frame_[i];
        }
        else
        {
            for(int i = 0; i < samples; i++)
                pcm[i] = (short)(((int)frame_[2 * i] + (int)frame_[2 * i + 1]) / 2);
        }
    }

    mp3dec_t dec_;
    InputQueue in_;
    PcmQueue pcm_;
    bool eof_, need_, done_;
    int channels_, rate_;
    mp3d_sample_t frame_[MINIMP3_MAX_SAMPLES_PER_FRAME];
};

// ---- whole-file decoders -------------------------------------------------------

class OggWhole : public BlockDecoder
{
public:
    OggWhole() : v_(NULL), channels_(0), rate_(0), total_(0), done_(false) {}

    ~OggWhole()
    {
        if(v_)
            stb_vorbis_close(v_);
    }

    bool open(const void *data, int32_t len)
    {
        int error = 0;
        v_ = stb_vorbis_open_memory((const unsigned char *)data, (int)len, &error, NULL);

        if(!v_)
            return false;

        stb_vorbis_info info = stb_vorbis_get_info(v_);
        channels_ = info.channels >= 2 ? 2 : 1;
        rate_ = (int)info.sample_rate;
        total_ = (int32_t)stb_vorbis_stream_length_in_samples(v_);
        return info.channels >= 1 && rate_ > 0;
    }

    int channels() const { return channels_; }
    int rate() const { return rate_; }
    void feed(const void *, int32_t, bool) {}
    bool hungry() const { return false; }
    bool finished() const { return done_; }
    bool seekable() const { return true; }
    int32_t total_frames() const { return total_; }

    int32_t decode(short *out, int32_t frames)
    {
        int32_t n = 0;

        while(n < frames && !done_)
        {
            int got = stb_vorbis_get_samples_short_interleaved(v_, channels_, out + n * channels_, (frames - n) * channels_);

            if(got <= 0)
                done_ = true;
            else
                n += got;
        }

        return n;
    }

    bool seek(int32_t frame)
    {
        done_ = false;

        if(frame <= 0)
            return stb_vorbis_seek_start(v_) != 0;

        if(total_ > 0 && frame >= total_)
            frame = total_ - 1;

        return stb_vorbis_seek(v_, (unsigned int)frame) != 0;
    }

private:
    stb_vorbis *v_;
    int channels_, rate_;
    int32_t total_;
    bool done_;
};

class Mp3Whole : public BlockDecoder
{
public:
    Mp3Whole() : data_(NULL), len_(0), pos_(0), channels_(0), rate_(0), total_(0), done_(false)
    {
        mp3dec_init(&dec_);
    }

    bool open(const void *data, int32_t len)
    {
        data_ = (const unsigned char *)data;
        len_ = len;
        long at = 0;
        int32_t frame = 0;

        // Find where every frame starts, and how many samples it holds.
        while(at < len_)
        {
            mp3dec_frame_info_t info;
            int samples = mp3dec_decode_frame(&dec_, data_ + at, (int)(len_ - at), NULL, &info);

            if(info.frame_bytes <= 0)
                break;

            if(samples > 0)
            {
                if(!channels_)
                {
                    channels_ = info.channels >= 2 ? 2 : 1;
                    rate_ = info.hz;
                }

                Entry e;
                e.offset = at;
                e.first = frame;
                index_.push_back(e);
                frame += samples;
            }

            at += info.frame_bytes;
        }

        total_ = frame;
        mp3dec_init(&dec_);
        return !index_.empty() && rate_ > 0;
    }

    int channels() const { return channels_; }
    int rate() const { return rate_; }
    void feed(const void *, int32_t, bool) {}
    bool hungry() const { return false; }
    bool finished() const { return done_; }
    bool seekable() const { return true; }
    int32_t total_frames() const { return total_; }

    int32_t decode(short *out, int32_t frames)
    {
        int32_t n = 0;

        while(n < frames)
        {
            if(!pcm_.empty())
            {
                n += pcm_.take(out + n * channels_, frames - n, channels_);
                continue;
            }

            if(done_)
                break;

            int samples = decode_one();

            if(samples < 0)
                done_ = true;
        }

        return n;
    }

    bool seek(int32_t target)
    {
        done_ = false;
        pcm_.refill().clear();
        mp3dec_init(&dec_);

        if(target < 0)
            target = 0;

        // The last frame starting at or before the target; two earlier ones warm up
        // the decoder (a frame's bits can start in the frames before it).
        size_t low = 0, high = index_.size();

        while(high - low > 1)
        {
            size_t mid = (low + high) / 2;

            if(index_[mid].first <= target)
                low = mid;
            else
                high = mid;
        }

        size_t start = low >= 2 ? low - 2 : 0;
        pos_ = index_[start].offset;

        for(size_t i = start; i < low; ++i)
            decode_one();

        pcm_.refill().clear();

        if(decode_one() < 0)
        {
            done_ = true;
            return false;
        }

        pcm_.drop(target - index_[low].first, channels_);
        return true;
    }

private:
    // Decodes the frame at pos_ into pcm_. Returns its samples, -1 at the end.
    int decode_one()
    {
        if(pos_ >= len_)
            return -1;

        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&dec_, data_ + pos_, (int)(len_ - pos_), frame_, &info);

        if(info.frame_bytes <= 0)
            return -1;

        pos_ += info.frame_bytes;

        if(samples > 0)
        {
            std::vector<short> &pcm = pcm_.refill();
            pcm.resize((size_t)samples * channels_);

            if(info.channels == channels_)
                memcpy(&pcm[0], frame_, (size_t)samples * channels_ * sizeof(short));
            else if(channels_ == 2)
            {
                for(int i = 0; i < samples; i++)
                    pcm[2 * i] = pcm[2 * i + 1] = frame_[i];
            }
            else
            {
                for(int i = 0; i < samples; i++)
                    pcm[i] = (short)(((int)frame_[2 * i] + (int)frame_[2 * i + 1]) / 2);
            }
        }

        return samples;
    }

    struct Entry
    {
        long offset;
        int32_t first;
    };

    mp3dec_t dec_;
    const unsigned char *data_;
    long len_, pos_;
    int channels_, rate_;
    int32_t total_;
    bool done_;
    std::vector<Entry> index_;
    PcmQueue pcm_;
    mp3d_sample_t frame_[MINIMP3_MAX_SAMPLES_PER_FRAME];
};

} // namespace

BlockDecoder *open_ogg_seekable(const void *data, int32_t len)
{
    OggWhole *d = new OggWhole;

    if(!d->open(data, len))
    {
        delete d;
        return NULL;
    }

    return d;
}

BlockDecoder *open_mp3_seekable(const void *data, int32_t len)
{
    Mp3Whole *d = new Mp3Whole;

    if(!d->open(data, len))
    {
        delete d;
        return NULL;
    }

    return d;
}

bool decode_ogg_sample(const void *data, int32_t len, short **pcm, int *channels, int *rate, int32_t *frames)
{
    int ch = 0, hz = 0;
    short *out = NULL;
    int got = stb_vorbis_decode_memory((const unsigned char *)data, (int)len, &ch, &hz, &out);

    if(got <= 0 || !out)
    {
        if(out)
            free(out);

        return false;
    }

    *pcm = out;
    *channels = ch;
    *rate = hz;
    *frames = got;
    return true;
}

BlockDecoder *open_ogg(const void *data, int32_t len, bool last)
{
    OggDecoder *d = new OggDecoder;

    if(!d->open(data, len, last))
    {
        delete d;
        return NULL;
    }

    return d;
}

BlockDecoder *open_mp3(const void *data, int32_t len, bool last)
{
    Mp3Decoder *d = new Mp3Decoder;

    if(!d->open(data, len, last))
    {
        delete d;
        return NULL;
    }

    return d;
}

// ---- tracker modules (DUMB) -----------------------------------------------------

struct Module
{
    DUH *duh;
};

struct ModulePlayer
{
    DUH_SIGRENDERER *sr;
    sample_t **buf;
    long buf_size;
};

void module_init()
{
    // src/zcmusic.cpp: dumb_resampling_quality = 1, linear interpolation in
    // DUMB 0.9.3 (DUMB 2 numbers its levels differently).
    dumb_resampling_quality = DUMB_RQ_LINEAR;
    // DUMB 0.9.3 pans MOD channels left/right/right/left at 16/48 of 64 (S3M
    // at 3/12 of 15); DUMB 2 narrows that to 25% by default. 50% gives 0.9.3's
    // MOD panning (S3M: 3/11).
    dumb_it_default_panning_separation = 50;
}

void module_exit()
{
    dumb_exit();
}

Module *module_load(const void *data, int32_t size, ModuleKind kind)
{
    if(size <= 0)
        return NULL;

    DUMBFILE *f = dumbfile_open_memory((const char *)data, (size_t)size);

    if(!f)
        return NULL;

    DUH *duh = NULL;

    switch(kind)
    {
    case MODULE_IT:
        duh = dumb_read_it_quick(f);
        break;

    case MODULE_XM:
        duh = dumb_read_xm_quick(f);
        break;

    case MODULE_S3M:
        duh = dumb_read_s3m_quick(f);
        break;

    case MODULE_MOD:
        // 0: no restrictions; DUMB 0.9.3 loads 15-sample modules too.
        duh = dumb_read_mod_quick(f, 0);
        break;
    }

    dumbfile_close(f);

    if(!duh)
        return NULL;

    Module *m = new Module;
    m->duh = duh;
    return m;
}

void module_unload(Module *m)
{
    if(!m)
        return;

    unload_duh(m->duh);
    delete m;
}

ModulePlayer *module_start(Module *m, int channels, int32_t pos)
{
    if(!m)
        return NULL;

    // Like DUMB 0.9.3, a module plays on at its end (no loop callback).
    DUH_SIGRENDERER *sr = duh_start_sigrenderer(m->duh, 0, channels, pos);

    if(!sr)
        return NULL;

    ModulePlayer *p = new ModulePlayer;
    p->sr = sr;
    p->buf = NULL;
    p->buf_size = 0;
    return p;
}

void module_stop(ModulePlayer *p)
{
    if(!p)
        return;

    duh_end_sigrenderer(p->sr);
    destroy_sample_buffer(p->buf);
    delete p;
}

int32_t module_render(ModulePlayer *p, float volume, int freq, int32_t frames, void *out)
{
    if(!p || freq <= 0)
        return 0;

    const int channels = duh_sigrenderer_get_n_channels(p->sr);
    const float delta = 65536.0f / (float)freq;
    unsigned short *dst = (unsigned short *)out;
    long done = 0;
    int empty = 0;

    // DUMB 2 returns early once, where the module first loops (its loop
    // detection); DUMB 0.9.3 renders straight on. Fewer frames than asked
    // only mean the end when rendering on gives nothing.
    while(done < frames)
    {
        long n = duh_render_int(p->sr, &p->buf, &p->buf_size, 16, 1, volume, delta, frames - done,
                                dst + (size_t)done * channels);

        if(n <= 0)
        {
            if(++empty >= 2)
                break;

            continue;
        }

        empty = 0;
        done += n;
    }

    return done;
}

int32_t module_length(Module *m)
{
    if(!m)
        return 0;

    long length = duh_get_length(m->duh);

    if(length > 0)
        return length;

    DUMB_IT_SIGDATA *sig = duh_get_it_sigdata(m->duh);

    if(sig)
        length = dumb_it_build_checkpoints(sig, 0);

    return length;
}

// ---- Game_Music_Emu -------------------------------------------------------------

struct Emu
{
    Music_Emu *emu;
    bool started;
};

Emu *emu_load(const char *filename, const char *ext, int32_t rate)
{
    Music_Emu *emu = NULL;

    if(same_ext(ext, "spc"))
        emu = new Spc_Emu;
    else if(same_ext(ext, "gbs"))
        emu = new Gbs_Emu;
    else if(same_ext(ext, "vgm"))
        emu = new Vgm_Emu;
    else if(same_ext(ext, "nsf"))
        emu = new Nsf_Emu;
    else if(same_ext(ext, "gym"))
        emu = new Gym_Emu;

    if(!emu)
        return NULL;

    Std_File_Reader in;
    blargg_err_t err = emu->set_sample_rate(rate);

    if(!err)
        err = in.open(filename);

    if(!err)
        err = emu->load(in);

    if(err)
    {
        delete emu;
        return NULL;
    }

    // Game_Music_Emu 0.3.0 (the Allegro build's) plays every track as it is:
    // no skipping of leading silence, no end of track after a silence, no
    // track length taken from the file.
    emu->ignore_silence(true);
    emu->set_autoload_playback_limit(false);

    Emu *e = new Emu;
    e->emu = emu;
    e->started = false;
    return e;
}

void emu_unload(Emu *e)
{
    if(!e)
        return;

    delete e->emu;
    delete e;
}

int emu_track_count(Emu *e)
{
    return e ? e->emu->track_count() : 0;
}

void emu_start_track(Emu *e, int track)
{
    if(!e)
        return;

    blargg_err_t err = e->emu->start_track(track);
    // An error that comes with an ended track is only a warning; the track
    // then plays silence. Anything else leaves no track to play.
    e->started = e->emu->current_track() >= 0 && (!err || e->emu->track_ended());
}

void emu_play(Emu *e, int32_t count, short *out)
{
    if(!e || !e->started)
    {
        memset(out, 0, (size_t)count * sizeof(short));
        return;
    }

    e->emu->play(count, out);
}

} // namespace zcmusic_decoders

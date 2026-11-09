/*
 * test_platform_audio_sdl_portonly.cpp - smoke test of the port's sound
 * output (the reference has no counterpart: its mixer output cannot be read
 * back). install_sound() opens the SDL audio device and the MIDI backend;
 * generated samples, an audio stream and a short MIDI play. The mixer's
 * output (tapped after mixing) must not be silent, follow volume, pan and
 * the voices' timing, and the MIDI backend must get the song with
 * controller 7 scaled by the MIDI volume. The mix is written to
 * <tmp>/audio_smoke.wav. Audible: a few tones, a buzz and four piano notes.
 */
#include "conf.h"

#include "zcport/midi_backend.h"
#include "zcport/platform_sound.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string.h>
#include <thread>
#include <vector>

namespace
{
    typedef std::chrono::steady_clock clk;

    void plain_wait(double ms)
    {
        clk::time_point t0 = clk::now();
        while(std::chrono::duration<double, std::milli>(clk::now() - t0).count() < ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const double kPi = 3.14159265358979323846;

    /* ---- the mixer's output ---------------------------------------------- */

    std::mutex g_cap_mutex;
    std::vector<short> g_cap;                  /* stereo frames of this phase */
    std::vector<short> g_all;                  /* everything, for the WAV */
    const size_t kAllMax = 44100 * 2 * 10;

    void tap(void *, const short *frames, int count)
    {
        std::lock_guard<std::mutex> lock(g_cap_mutex);
        g_cap.insert(g_cap.end(), frames, frames + 2 * count);
        if(g_all.size() < kAllMax)
            g_all.insert(g_all.end(), frames, frames + 2 * count);
    }

    struct Span
    {
        int peak_l, peak_r;
        long first, last;                      /* non-silent frames, -1 if none */
        long frames;
    };

    Span take_phase()
    {
        std::lock_guard<std::mutex> lock(g_cap_mutex);
        Span s = { 0, 0, -1, -1, (long)(g_cap.size() / 2) };
        for(long i = 0; i < s.frames; i++)
        {
            int l = g_cap[2 * i], r = g_cap[2 * i + 1];
            l = l < 0 ? -l : l;
            r = r < 0 ? -r : r;
            if(l > s.peak_l) s.peak_l = l;
            if(r > s.peak_r) s.peak_r = r;
            if(l > 64 || r > 64)
            {
                if(s.first < 0)
                    s.first = i;
                s.last = i;
            }
        }
        g_cap.clear();
        return s;
    }

    /* The left channel of this phase, from the first to the last non-silent
     * frame. */
    std::vector<int> take_left()
    {
        std::lock_guard<std::mutex> lock(g_cap_mutex);
        std::vector<int> l;
        long n = (long)(g_cap.size() / 2), first = -1, last = -1;
        for(long i = 0; i < n; i++)
            if(g_cap[2 * i] > 64 || g_cap[2 * i] < -64)
            {
                if(first < 0)
                    first = i;
                last = i;
            }
        for(long i = first; first >= 0 && i <= last; i++)
            l.push_back(g_cap[2 * i]);
        g_cap.clear();
        return l;
    }

    /* Rising and falling steps, and the largest step. */
    void steps(const std::vector<int> &l, int *up, int *down, int *jump)
    {
        *up = *down = *jump = 0;
        for(size_t i = 1; i < l.size(); i++)
        {
            int d = l[i] - l[i - 1];
            if(d > 0) (*up)++;
            if(d < 0) (*down)++;
            if((d < 0 ? -d : d) > *jump) *jump = d < 0 ? -d : d;
        }
    }

    void write_wav(const std::string &path, const std::vector<short> &pcm, int rate)
    {
        FILE *f = fopen(path.c_str(), "wb");
        if(!f)
            return;
        unsigned data = (unsigned)(pcm.size() * 2);
        unsigned char h[44];
        memcpy(h, "RIFF", 4);
        unsigned v = 36 + data; memcpy(h + 4, &v, 4);
        memcpy(h + 8, "WAVEfmt ", 8);
        v = 16; memcpy(h + 16, &v, 4);
        unsigned short w = 1; memcpy(h + 20, &w, 2);          /* PCM */
        w = 2; memcpy(h + 22, &w, 2);                          /* stereo */
        v = (unsigned)rate; memcpy(h + 24, &v, 4);
        v = (unsigned)rate * 4; memcpy(h + 28, &v, 4);
        w = 4; memcpy(h + 32, &w, 2);
        w = 16; memcpy(h + 34, &w, 2);
        memcpy(h + 36, "data", 4);
        memcpy(h + 40, &data, 4);
        fwrite(h, 1, 44, f);
        if(!pcm.empty())
            fwrite(&pcm[0], 2, pcm.size(), f);
        fclose(f);
    }

    /* ---- MIDI backend that records and forwards ---------------------------- */

    const ZCPORT_MIDI_BACKEND *g_real = NULL;
    std::mutex g_midi_mutex;
    std::vector<unsigned> g_msgs;

    int w_open(void) { return (g_real && g_real->open) ? g_real->open() : 0; }
    void w_close(void) { if(g_real && g_real->close) g_real->close(); }
    void w_short(unsigned msg)
    {
        {
            std::lock_guard<std::mutex> lock(g_midi_mutex);
            g_msgs.push_back(msg);
        }
        if(g_real && g_real->short_msg)
            g_real->short_msg(msg);
    }
    void w_long(const unsigned char *d, int n) { if(g_real && g_real->long_msg) g_real->long_msg(d, n); }
    void w_reset(void) { if(g_real && g_real->reset) g_real->reset(); }

    ZCPORT_MIDI_BACKEND g_wrap = { "smoke test MIDI", w_open, w_close, w_short, w_long, w_reset, NULL };

    int count_msgs(unsigned mask, unsigned value)
    {
        std::lock_guard<std::mutex> lock(g_midi_mutex);
        int n = 0;
        for(size_t i = 0; i < g_msgs.size(); i++)
            if((g_msgs[i] & mask) == value)
                n++;
        return n;
    }

    /* ---- sounds --------------------------------------------------------------- */

    struct Tone8                               /* 8-bit mono sine */
    {
        SAMPLE spl;
        std::vector<unsigned char> data;
        Tone8(int freq, int len, double hz, int amp)
        {
            data.resize((size_t)len);
            for(int i = 0; i < len; i++)
                data[i] = (unsigned char)(128 + (int)lrint(amp * sin(2 * kPi * hz * i / freq)));
            memset(&spl, 0, sizeof(spl));
            spl.bits = 8;
            spl.freq = freq;
            spl.priority = 128;
            spl.len = (unsigned long)len;
            spl.loop_end = (unsigned long)len;
            spl.data = &data[0];
        }
    };

    struct Tone16                              /* 16-bit stereo, unsigned */
    {
        SAMPLE spl;
        std::vector<unsigned short> data;
        Tone16(int freq, int len, double hz_l, double hz_r, int amp)
        {
            data.resize((size_t)len * 2);
            for(int i = 0; i < len; i++)
            {
                data[2 * i] = (unsigned short)(0x8000 + (int)lrint(amp * sin(2 * kPi * hz_l * i / freq)));
                data[2 * i + 1] = (unsigned short)(0x8000 + (int)lrint(amp * sin(2 * kPi * hz_r * i / freq)));
            }
            memset(&spl, 0, sizeof(spl));
            spl.bits = 16;
            spl.stereo = 1;
            spl.freq = freq;
            spl.priority = 128;
            spl.len = (unsigned long)len;
            spl.loop_end = (unsigned long)len;
            spl.data = &data[0];
        }
    };

    struct Ramp                                /* 16-bit mono, rising, 1000 samples */
    {
        SAMPLE spl;
        unsigned short data[1000];
        Ramp()
        {
            for(int i = 0; i < 1000; i++)
                data[i] = (unsigned short)(0x8000 - 20000 + 40 * i);
            memset(&spl, 0, sizeof(spl));
            spl.bits = 16;
            spl.freq = 44100;                  /* one sample per output frame */
            spl.priority = 128;
            spl.len = 1000;
            spl.loop_end = 1000;
            spl.data = data;
        }
    };

    struct Song                                /* C E G C, half a beat each */
    {
        MIDI midi;
        std::vector<unsigned char> trk;
        void ev(unsigned delta, const unsigned char *b, int n)
        {
            trk.push_back((unsigned char)delta);   /* deltas < 128 */
            trk.insert(trk.end(), b, b + n);
        }
        Song()
        {
            memset(&midi, 0, sizeof(midi));
            midi.divisions = 96;
            const unsigned char tempo[] = { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 };   /* 120 bpm */
            const unsigned char prog[] = { 0xC0, 0x00 };
            const unsigned char vol[] = { 0xB0, 0x07, 100 };
            ev(0, tempo, sizeof(tempo));
            ev(0, prog, sizeof(prog));
            ev(0, vol, sizeof(vol));
            const int notes[] = { 60, 64, 67, 72 };
            for(int k = 0; k < 4; k++)
            {
                unsigned char on[] = { 0x90, (unsigned char)notes[k], 90 };
                unsigned char off[] = { 0x80, (unsigned char)notes[k], 64 };
                ev(0, on, 3);
                ev(48, off, 3);
            }
            const unsigned char end[] = { 0xFF, 0x2F, 0x00 };
            ev(0, end, 3);
            midi.track[0].data = &trk[0];
            midi.track[0].len = (int)trk.size();
        }
    };
}

CONF_TEST(audio_output)
{
    g_real = zcport_get_midi_backend();
    zcport_set_midi_backend(&g_wrap);
    zcport_sound_set_tap(tap, NULL);
    install_timer();
    int r = install_sound(DIGI_AUTODETECT, MIDI_AUTODETECT, NULL);
    ctx.expect("install_sound", r == 0, "%d", r);
    if(r != 0 || !zcport_sound_output_active())
    {
        if(r == 0)
            remove_sound();
        zcport_sound_set_tap(NULL, NULL);
        zcport_set_midi_backend(g_real);
        ctx.skip("no audio device");
        return;
    }
    ctx.note("digi '%s', midi '%s' (forwarding to '%s')", digi_driver->name, midi_driver->name,
             g_real ? g_real->name : "nothing");
    set_volume(255, 128);

    /* idle: the device runs, the mix is silence */
    plain_wait(300);
    Span s = take_phase();
    ctx.note("idle: %ld frames in 300 ms", s.frames);
    ctx.expect("idle_runs", s.frames >= 44100 * 2 / 10, "%ld frames", s.frames);
    ctx.expect("idle_silent", s.peak_l == 0 && s.peak_r == 0, "peak %d/%d", s.peak_l, s.peak_r);

    /* 8-bit mono 440 Hz, 0.6 s at 22050 Hz, hard left */
    Tone8 t8(22050, 13230, 440.0, 100);
    int v = play_sample(&t8.spl, 160, 0, 1000, FALSE);
    ctx.expect("tone_voice", v >= 0, "%d", v);
    plain_wait(300);
    int pos = voice_get_position(v);
    ctx.expect("tone_playing", pos > 4000 && pos < 9000, "%d", pos);
    plain_wait(600);
    ctx.check_int("tone_pos_end", voice_get_position(v));
    plain_wait(200);
    s = take_phase();
    long span = s.first >= 0 ? s.last - s.first + 1 : 0;
    ctx.note("tone: peak L %d R %d, %ld frames heard", s.peak_l, s.peak_r, span);
    /* 100/128 of full scale * 160/255 on the left channel only */
    ctx.expect("tone_left_level", s.peak_l > 14000 && s.peak_l < 18000, "%d", s.peak_l);
    ctx.expect("tone_right_silent", s.peak_r == 0, "%d", s.peak_r);
    /* 0.6 s at the device rate: the sample's own rate resampled */
    ctx.expect("tone_length", span > 26000 && span < 26600, "%ld", span);

    /* 16-bit stereo, looping, centred: half volume per side */
    Tone16 t16(32000, 8000, 523.25, 659.25, 8000);
    v = play_sample(&t16.spl, 255, 128, 1000, TRUE);
    plain_wait(700);
    ctx.expect("loop_playing", voice_get_position(v) >= 0, "%d", voice_get_position(v));
    stop_sample(&t16.spl);
    plain_wait(200);
    s = take_phase();
    span = s.first >= 0 ? s.last - s.first + 1 : 0;
    ctx.note("stereo loop: peak L %d R %d, %ld frames heard", s.peak_l, s.peak_r, span);
    ctx.expect("loop_levels", s.peak_l > 3700 && s.peak_l < 4300 && s.peak_r > 3700 && s.peak_r < 4300,
               "%d %d", s.peak_l, s.peak_r);
    ctx.expect("loop_continues", span > 44100 * 6 / 10, "%ld", span);

    /* an audio stream fed with a 16-bit stereo sine */
    AUDIOSTREAM *st = play_audio_stream(1024, 16, TRUE, 22050, 255, 128);
    ctx.expect("stream", st != NULL);
    if(st)
    {
        long n = 0;
        clk::time_point t0 = clk::now();
        while(std::chrono::duration<double, std::milli>(clk::now() - t0).count() < 600)
        {
            /* every buffer available at once, as a fast decoder does */
            while(unsigned short *b = (unsigned short *)get_audio_stream_buffer(st))
            {
                for(int i = 0; i < 1024; i++, n++)
                {
                    unsigned short x = (unsigned short)(0x8000 + (int)lrint(6000 * sin(2 * kPi * 330.0 * n / 22050)));
                    b[2 * i] = b[2 * i + 1] = x;
                }
                free_audio_stream_buffer(st);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        stop_audio_stream(st);
    }
    plain_wait(100);
    {
        /* the sine is heard from the start and whole: no step larger than a
         * 330 Hz sine makes, no silent gap (the halves are refilled while
         * the mixer still plays their end) */
        std::vector<int> l = take_left();
        int up, down, jump, peak = 0, run = 0, gap = 0;
        steps(l, &up, &down, &jump);
        for(size_t i = 0; i < l.size(); i++)
        {
            int a = l[i] < 0 ? -l[i] : l[i];
            if(a > peak)
                peak = a;
            run = a <= 64 ? run + 1 : 0;
            if(run > gap)
                gap = run;
        }
        ctx.note("stream: %d frames, peak %d, largest step %d, longest quiet run %d frames",
                 (int)l.size(), peak, jump, gap);
        ctx.expect("stream_audible", peak > 2500, "%d", peak);
        ctx.expect("stream_continuous", l.size() > 22000 && jump < 400 && gap < 20, "%d frames, step %d, gap %d",
                   (int)l.size(), jump, gap);
    }

    /* play modes on a rising ramp: BIDIR goes up and down without a jump,
     * a forward loop jumps back, BACKWARD falls */
    Ramp ramp;
    const int modes[3] = { PLAYMODE_LOOP | PLAYMODE_BIDIR, PLAYMODE_LOOP, PLAYMODE_BACKWARD };
    const char *names[3] = { "bidir", "loop", "backward" };
    for(int m = 0; m < 3; m++)
    {
        v = allocate_voice(&ramp.spl);
        voice_set_pan(v, 0);
        voice_set_playmode(v, modes[m]);
        voice_start(v);
        plain_wait(m < 2 ? 300 : 100);
        deallocate_voice(v);
        plain_wait(100);
        std::vector<int> l = take_left();
        int up, down, jump;
        steps(l, &up, &down, &jump);
        ctx.note("%s: %d frames, %d up %d down, largest step %d", names[m], (int)l.size(), up, down, jump);
        if(m == 0)
            ctx.expect("bidir_pingpong", up > 4000 && down > 4000 && jump < 200, "%d %d %d", up, down, jump);
        else if(m == 1)
            ctx.expect("loop_wraps", up > 8000 && down < 100 && jump > 15000, "%d %d %d", up, down, jump);
        else
            ctx.expect("backward_falls", down > 900 && up < 10, "%d %d", up, down);
    }

    /* MIDI: four notes; controller 7 (100) goes out scaled by the MIDI volume */
    {
        std::lock_guard<std::mutex> lock(g_midi_mutex);
        g_msgs.clear();
    }
    Song song;
    ctx.check_int("play_midi", play_midi(&song.midi, FALSE));
    plain_wait(2300);
    ctx.check_int("midi_pos_end", (int)midi_pos);
    int notes_on = count_msgs(0xFF00FF, 0x5A0090);                 /* 90 nn 5A */
    int notes_off = count_msgs(0xFF00FF, 0x000080);                /* 80 nn 00 */
    int cc7 = count_msgs(0xFFFFFF, 0x3207B0);                      /* B0 07 32 */
    int resets = count_msgs(0xFFFFF0, 0x0079B0);                   /* Bc 79 00 */
    ctx.note("MIDI: %d note-ons, %d note-offs, %d x B0 07 32, %d resets", notes_on, notes_off, cc7, resets);
    ctx.expect("midi_notes", notes_on == 4 && notes_off == 4, "%d %d", notes_on, notes_off);
    ctx.expect("midi_volume_cc7", cc7 >= 1, "%d", cc7);
    ctx.expect("midi_reset", resets == 16, "%d", resets);
    stop_midi();

    remove_sound();
    ctx.check_bool("inactive_after_remove", zcport_sound_output_active() == 0);
    zcport_sound_set_tap(NULL, NULL);
    zcport_set_midi_backend(g_real);
    {
        std::lock_guard<std::mutex> lock(g_cap_mutex);
        write_wav(ctx.tmp("audio_smoke.wav"), g_all, 44100);
        ctx.note("mix written to %s (%u frames)", ctx.tmp("audio_smoke.wav").c_str(), (unsigned)(g_all.size() / 2));
    }
}

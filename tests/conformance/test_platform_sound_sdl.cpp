/*
 * test_platform_sound_sdl.cpp - sound system state, voices, MIDI timing and
 * audio streams (platform module; what the game reads back must behave the
 * same, with or without a sound device; the port's audible output is checked
 * by test_platform_audio_sdl_portonly.cpp).
 *
 * Nothing audible is played on the reference: samples are silence at
 * volume 0 and the MIDI file has no notes (only tempo and end of track).
 * Timed values are checked as tolerance ranges (ok/FAIL); the measured
 * values go to notes.
 */
#include "conf.h"

#include <chrono>
#include <string.h>
#include <thread>
#include <vector>

namespace
{
    typedef std::chrono::steady_clock clk;

    double ms_since(clk::time_point t0)
    {
        return std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    }

    void plain_wait(double ms)
    {
        clk::time_point t0 = clk::now();
        while(ms_since(t0) < ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    bool g_sound_ok = false;

    bool sound_up(conf::Ctx &ctx)
    {
        if(!g_sound_ok)
        {
            ctx.skip("install_sound failed");
            return false;
        }
        return true;
    }

    /* 8-bit mono silence (unsigned samples: 0x80). */
    struct SilentSample
    {
        SAMPLE spl;
        std::vector<unsigned char> data;
        SilentSample(int freq, int len, int bits = 8)
        {
            data.assign((size_t)len * (bits / 8), bits == 8 ? 0x80 : 0);
            memset(&spl, 0, sizeof(spl));
            spl.bits = bits;
            spl.stereo = 0;
            spl.freq = freq;
            spl.priority = 128;
            spl.len = (unsigned long)len;
            spl.loop_start = 0;
            spl.loop_end = (unsigned long)len;
            spl.data = &data[0];
        }
    };

    /* A MIDI file with no notes: tempo 120 bpm, 'beats' beats long. */
    struct SilentMidi
    {
        MIDI midi;
        std::vector<unsigned char> track;
        SilentMidi(int beats, int divisions = 96, unsigned tempo = 500000)
        {
            memset(&midi, 0, sizeof(midi));
            midi.divisions = divisions;
            unsigned char head[] = { 0x00, 0xFF, 0x51, 0x03,
                                     (unsigned char)(tempo >> 16), (unsigned char)(tempo >> 8), (unsigned char)tempo };
            track.insert(track.end(), head, head + sizeof(head));
            /* one beat at a time: meta text events (no sound) */
            for(int b = 0; b < beats; b++)
            {
                unsigned d = (unsigned)divisions;
                if(d >= 128)
                {
                    track.push_back((unsigned char)(0x80 | (d >> 7)));
                    track.push_back((unsigned char)(d & 0x7F));
                }
                else
                    track.push_back((unsigned char)d);
                unsigned char txt[] = { 0xFF, 0x01, 0x01, 'x' };
                track.insert(track.end(), txt, txt + sizeof(txt));
            }
            unsigned char end[] = { 0x00, 0xFF, 0x2F, 0x00 };
            track.insert(track.end(), end, end + sizeof(end));
            midi.track[0].data = &track[0];
            midi.track[0].len = (int)track.size();
        }
    };
}

CONF_TEST(sound_install)
{
    install_timer();
    ctx.check_bool("digi_driver_before", digi_driver != NULL);
    ctx.check_bool("midi_driver_before", midi_driver != NULL);
    if(midi_driver)
        ctx.check_bool("midi_before_is_digmid", strcmp(midi_driver->name, "DIGMID") == 0);
    int r = install_sound(DIGI_AUTODETECT, MIDI_AUTODETECT, NULL);
    ctx.check_int("install_sound", r);
    g_sound_ok = (r == 0);
    if(!g_sound_ok)
        return;
    ctx.check_int("install_again", install_sound(DIGI_AUTODETECT, MIDI_AUTODETECT, NULL));
    ctx.check_bool("digi_driver", digi_driver != NULL);
    ctx.check_bool("midi_driver", midi_driver != NULL);
    ctx.note("digi '%s' id %08x voices %d; midi '%s' id %08x voices %d", digi_driver->name, digi_driver->id,
             digi_driver->voices, midi_driver->name, midi_driver->id, midi_driver->voices);
    ctx.check_bool("midi_is_digmid", strcmp(midi_driver->name, "DIGMID") == 0);
    ctx.check_bool("digi_is_none", digi_driver->id == DIGI_NONE);
    ctx.check_bool("midi_is_none", midi_driver->id == MIDI_NONE);
}

CONF_TEST(sound_volume)
{
    if(!sound_up(ctx))
        return;
    int d = -2, m = -2;
    get_volume(&d, &m);
    ctx.checkf("initial", "%d %d", d, m);
    set_volume(100, 50);
    get_volume(&d, &m);
    ctx.checkf("set_100_50", "%d %d", d, m);
    set_volume(-1, 30);
    get_volume(&d, &m);
    ctx.checkf("set_keep_30", "%d %d", d, m);
    set_volume(70, -1);
    get_volume(&d, &m);
    ctx.checkf("set_70_keep", "%d %d", d, m);
    set_volume(300, -5);
    get_volume(&d, &m);
    ctx.checkf("set_out_of_range", "%d %d", d, m);
    set_volume(255, 255);
    get_volume(&d, &m);
    ctx.checkf("set_255", "%d %d", d, m);
}

CONF_TEST(sound_voice_allocation)
{
    if(!sound_up(ctx))
        return;
    SilentSample s(11025, 11025);
    int one = allocate_voice(&s.spl);
    ctx.check_int("first", one);
    if(one >= 0)
    {
        ctx.check_int("pos_unstarted", voice_get_position(one));
        ctx.check_int("freq_default", voice_get_frequency(one));
        ctx.check_int("vol_default", voice_get_volume(one));
        ctx.check_int("pan_default", voice_get_pan(one));
        ctx.check_bool("check_is_sample", voice_check(one) == &s.spl);
        deallocate_voice(one);
    }
    std::vector<int> v;
    for(int i = 0; i < 300; i++)
    {
        int n = allocate_voice(&s.spl);
        if(n < 0)
            break;
        v.push_back(n);
    }
    ctx.check_int("count", (int)v.size());
    conf::Seq ids(ctx, "ids");
    for(size_t i = 0; i < v.size(); i++)
        ids.add_int(v[i], "voice %d", (int)i);
    ids.end();
    for(size_t i = 0; i < v.size(); i++)
        deallocate_voice(v[i]);
    if(!v.empty())
        ctx.check_bool("check_after_dealloc", voice_check(v[0]) == NULL);
    int again = allocate_voice(&s.spl);
    ctx.check_int("first_again", again);
    if(again >= 0)
        deallocate_voice(again);
}

CONF_TEST(sound_voice_timing)
{
    if(!sound_up(ctx))
        return;
    SilentSample s(11025, 11025);              /* 1 second */
    int v = allocate_voice(&s.spl);
    ctx.check_bool("allocated", v >= 0);
    if(v < 0)
        return;
    voice_set_volume(v, 0);
    ctx.check_int("pos_before_start", voice_get_position(v));
    voice_start(v);
    plain_wait(250);
    int p = voice_get_position(v);
    ctx.note("after 250 ms: %d", p);
    ctx.expect("pos_250ms", p >= 1500 && p <= 4500, "%d", p);
    plain_wait(1100);
    p = voice_get_position(v);
    ctx.check_int("pos_after_end", p);
    ctx.check_bool("check_after_end", voice_check(v) != NULL);

    /* restart from a position, loop */
    voice_set_position(v, 0);
    voice_set_playmode(v, PLAYMODE_LOOP);
    voice_start(v);
    plain_wait(1300);
    p = voice_get_position(v);
    ctx.note("looping after 1300 ms: %d", p);
    ctx.expect("pos_looping", p >= 1000 && p <= 6000, "%d", p);
    voice_stop(v);
    int ps = voice_get_position(v);
    plain_wait(200);
    ctx.expect("stopped_holds", voice_get_position(v) == ps, "%d -> %d", ps, voice_get_position(v));
    voice_set_position(v, 5000);
    ctx.check_int("set_position_stopped", voice_get_position(v));
    voice_set_frequency(v, 22050);
    ctx.check_int("frequency", voice_get_frequency(v));
    voice_start(v);
    plain_wait(250);
    p = voice_get_position(v);
    ctx.note("double speed from 5000 after 250 ms: %d", p);
    ctx.expect("pos_double_speed", p >= 7500 && p <= 12000, "%d", p);
    voice_set_playmode(v, PLAYMODE_PLAY);
    plain_wait(900);
    ctx.check_int("pos_after_playmode_play", voice_get_position(v));
    voice_set_pan(v, 30);
    ctx.check_int("pan", voice_get_pan(v));
    voice_set_volume(v, 77);
    ctx.check_int("volume", voice_get_volume(v));
    voice_set_volume(v, 0);
    deallocate_voice(v);
}

CONF_TEST(sound_play_sample)
{
    if(!sound_up(ctx))
        return;
    SilentSample s(11025, 5513);               /* 0.5 seconds */
    int v = play_sample(&s.spl, 0, 128, 1000, FALSE);
    ctx.check_bool("voice", v >= 0);
    if(v < 0)
        return;
    plain_wait(100);
    int p = voice_get_position(v);
    ctx.expect("playing", p > 0, "%d", p);
    plain_wait(700);
    ctx.check_int("pos_finished", voice_get_position(v));
    ctx.check_bool("check_finished", voice_check(v) != NULL);
    int v2 = play_sample(&s.spl, 0, 128, 1000, TRUE);
    ctx.check_int("same_voice_again", v2 == v ? 1 : 0);
    plain_wait(700);
    p = voice_get_position(v2);
    ctx.expect("looping", p >= 0, "%d", p);
    adjust_sample(&s.spl, 0, 64, 2000, FALSE);
    ctx.check_int("adjusted_freq", voice_get_frequency(v2));
    ctx.check_int("adjusted_pan", voice_get_pan(v2));
    stop_sample(&s.spl);
    ctx.check_bool("check_after_stop", voice_check(v2) == NULL);
    ctx.check_int("pos_after_stop", voice_get_position(v2));
}

CONF_TEST(sound_midi_play)
{
    if(!sound_up(ctx))
        return;
    ctx.check_int("pos_idle", (int)midi_pos);
    SilentMidi m(6);                           /* 6 beats at 120 bpm = 3 s */
    ctx.check_int("play", play_midi(&m.midi, FALSE));
    ctx.check_int("pos_start", (int)midi_pos);
    plain_wait(1250);
    int p = (int)midi_pos;
    ctx.note("after 1250 ms: midi_pos %d midi_time %d", p, (int)midi_time);
    ctx.expect("pos_1250ms", p >= 2 && p <= 3, "%d", p);
    plain_wait(2300);
    ctx.check_int("pos_after_end", (int)midi_pos);
    ctx.note("midi_time after end: %d", (int)midi_time);
    ctx.check_int("loop_start_after", (int)midi_loop_start);
    ctx.check_int("loop_end_after", (int)midi_loop_end);
    stop_midi();
    ctx.check_int("pos_stopped", (int)midi_pos);
}

CONF_TEST(sound_midi_loop)
{
    if(!sound_up(ctx))
        return;
    SilentMidi m(4);                           /* 2 s */
    ctx.check_int("play_loop", play_midi(&m.midi, TRUE));
    plain_wait(2250);                          /* half a beat into the 2nd pass */
    int p = (int)midi_pos;
    ctx.note("looped after 2250 ms: %d", p);
    ctx.expect("pos_looped", p == 1, "%d", p);
    stop_midi();
    ctx.check_int("pos_stopped", (int)midi_pos);   /* the beat, negated */

    /* stopping in the middle of a pass */
    play_midi(&m.midi, TRUE);
    plain_wait(1250);                          /* beat 3 */
    stop_midi();
    ctx.check_int("pos_stopped_beat_3", (int)midi_pos);

    /* loop points (in beats), set after play_midi as the game does */
    ctx.check_int("play_points", play_midi(&m.midi, TRUE));
    midi_loop_start = 1;
    midi_loop_end = 2;
    plain_wait(2600);
    p = (int)midi_pos;
    ctx.note("loop 1..2 after 2600 ms: %d", p);
    ctx.expect("pos_in_loop", p >= 1 && p <= 2, "%d", p);
    stop_midi();
    ctx.check_int("loop_start_after_stop", (int)midi_loop_start);
    ctx.check_int("loop_end_after_stop", (int)midi_loop_end);
    midi_loop_start = -1;
    midi_loop_end = -1;
}

CONF_TEST(sound_midi_seek_pause)
{
    if(!sound_up(ctx))
        return;
    SilentMidi m(8);                           /* 4 s */
    play_midi(&m.midi, FALSE);
    ctx.check_int("seek_5", midi_seek(5));
    ctx.check_int("pos_after_seek", (int)midi_pos);
    midi_pause();
    int p0 = (int)midi_pos;
    plain_wait(1200);
    ctx.check_int("pos_paused", (int)midi_pos - p0);
    midi_resume();
    plain_wait(700);
    int p = (int)midi_pos;
    ctx.note("resumed 700 ms: %d", p);
    ctx.expect("pos_resumed", p >= 5 && p <= 6, "%d", p);
    ctx.check_int("seek_back_2", midi_seek(2));
    ctx.check_int("pos_seek_back", (int)midi_pos);
    ctx.check_int("seek_past_end", midi_seek(50));
    ctx.check_int("pos_past_end", (int)midi_pos);
    stop_midi();
    ctx.check_int("seek_stopped", midi_seek(1));
    ctx.check_int("pos_seek_stopped", (int)midi_pos);
    ctx.check_int("play_null", play_midi(NULL, FALSE));
    ctx.check_int("pos_null", (int)midi_pos);
}

CONF_TEST(sound_midi_tempo)
{
    if(!sound_up(ctx))
        return;
    SilentMidi m(8, 480, 250000);              /* 240 bpm: 2 s */
    play_midi(&m.midi, FALSE);
    plain_wait(1100);
    int p = (int)midi_pos;
    ctx.note("240 bpm after 1100 ms: %d", p);
    ctx.expect("pos_fast", p >= 4 && p <= 5, "%d", p);
    plain_wait(1300);
    ctx.check_int("pos_end", (int)midi_pos);
    stop_midi();
}

CONF_TEST(sound_stream)
{
    if(!sound_up(ctx))
        return;
    AUDIOSTREAM *st = play_audio_stream(1024, 16, TRUE, 22050, 0, 128);
    ctx.check_bool("stream", st != NULL);
    if(!st)
        return;
    ctx.check_bool("voice", st->voice >= 0);
    int buffers = 0, at_1s = -1;
    clk::time_point t0 = clk::now();
    bool first_soon = false;
    while(ms_since(t0) < 3000)
    {
        void *b = get_audio_stream_buffer(st);
        if(b)
        {
            if(buffers == 0 && ms_since(t0) < 50)
                first_soon = true;
            memset(b, 0, 1024 * 2 * 2);
            free_audio_stream_buffer(st);
            buffers++;
        }
        if(at_1s < 0 && ms_since(t0) >= 1000)
            at_1s = buffers;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ctx.note("stream buffers: %d in 3 s, %d in the first second", buffers, at_1s);
    ctx.check_bool("first_buffer_at_once", first_soon);
    /* steady state: 22050 / 1024 = 21.5 buffers a second */
    ctx.expect("rate", buffers - at_1s >= 38 && buffers - at_1s <= 48, "%d", buffers - at_1s);
    voice_stop(st->voice);
    int b = 0;
    t0 = clk::now();
    while(ms_since(t0) < 300)
    {
        if(get_audio_stream_buffer(st))
        {
            free_audio_stream_buffer(st);
            b++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ctx.note("buffers while stopped: %d", b);
    voice_start(st->voice);
    stop_audio_stream(st);
}

CONF_TEST(sound_remove)
{
    if(!sound_up(ctx))
        return;
    remove_sound();
    ctx.check_bool("digi_none", digi_driver != NULL && digi_driver->id == DIGI_NONE);
    ctx.check_bool("midi_none", midi_driver != NULL && midi_driver->id == MIDI_NONE);
    ctx.check_int("pos", (int)midi_pos);
    ctx.check_int("reinstall", install_sound(DIGI_AUTODETECT, MIDI_AUTODETECT, NULL));
    remove_sound();
    g_sound_ok = false;
}

// zcmusic_smoke: plays music files through the zplayer zcmusic API
// (src/port/game/zcmusic_port.cpp) on zcport's audio streams and writes what
// the streams receive to WAV files, with a short analysis of each.
//
//   zcmusic_smoke <out-dir> [-seconds N] [-bufsz KB] [-track N] <file>...
//
// For every file: zcmusic_load_file(), zcmusic_get_tracks() (and the header's
// count for NSF/GBS), zcmusic_change_track() with -track, zcmusic_play() at
// volume 255, then N seconds (default 6) of playing with zcmusic_poll() from
// this thread at 60 Hz (as the game's frames do) besides zcmusic's own 25 ms
// timer, a short pause in the middle (ZCMUSIC::position must not move), and
// zcmusic_stop() / zcmusic_unload_file(). The buffers handed to the stream go
// to <out-dir>/<file name>.wav; printed per channel: peak, RMS, clipped
// samples, and per second the RMS and a zero-crossing frequency estimate
// (exact for a pure tone).
//
// zcport's mixer paces the streams in real time, so each file takes N
// seconds. Built on request only: cmake --build <dir> --target zcmusic_smoke

#include <allegro.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "zcmusic.h"
#include "zcmusic_port.h"

namespace
{

struct Capture
{
    const ZCMUSIC *music;
    std::vector<short> samples;  // signed, interleaved
    int stereo;
    int freq;
    int buffers;
    int buffer_frames;
};

std::mutex g_capture_lock;
Capture g_capture;

void capture_buffer(const ZCMUSIC *music, const void *data, int frames, int stereo, int freq)
{
    std::lock_guard<std::mutex> guard(g_capture_lock);

    if(music != g_capture.music)
        return;

    const unsigned short *q = (const unsigned short *)data;
    const int count = frames * (stereo ? 2 : 1);

    for(int i = 0; i < count; i++)
        g_capture.samples.push_back((short)(q[i] ^ 0x8000));

    g_capture.stereo = stereo;
    g_capture.freq = freq;
    g_capture.buffers++;
    g_capture.buffer_frames = frames;
}

void put16(FILE *f, unsigned v)
{
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
}

void put32(FILE *f, unsigned long v)
{
    put16(f, (unsigned)(v & 0xFFFF));
    put16(f, (unsigned)(v >> 16));
}

bool write_wav(const std::string &path, const Capture &c)
{
    FILE *f = fopen(path.c_str(), "wb");

    if(!f)
        return false;

    const int ch = c.stereo ? 2 : 1;
    const unsigned long bytes = (unsigned long)c.samples.size() * 2;
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, ch);
    put32(f, c.freq);
    put32(f, (unsigned long)c.freq * ch * 2);
    put16(f, ch * 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, bytes);

    for(size_t i = 0; i < c.samples.size(); i++)
        put16(f, (unsigned)(unsigned short)c.samples[i]);

    fclose(f);
    return true;
}

double db(double rms)
{
    return rms > 0 ? 20.0 * log10(rms / 32768.0) : -999.0;
}

void analyse(const Capture &c)
{
    const int ch = c.stereo ? 2 : 1;
    const long frames = (long)(c.samples.size() / ch);
    printf("  stream: %d Hz, %s, %d samples per buffer, %d buffers, %.2f s captured\n",
           c.freq, c.stereo ? "stereo" : "mono", c.buffer_frames, c.buffers,
           c.freq ? (double)frames / c.freq : 0.0);

    for(int k = 0; k < ch; k++)
    {
        int peak = 0;
        long clipped = 0;
        double sum = 0;

        for(long i = 0; i < frames; i++)
        {
            int s = c.samples[(size_t)i * ch + k];
            int a = s < 0 ? -s : s;

            if(a > peak)
                peak = a;

            if(s >= 32767 || s <= -32768)
                clipped++;

            sum += (double)s * s;
        }

        printf("  %s: peak %d (%.1f dBFS), rms %.1f dBFS, clipped %ld\n",
               ch == 1 ? "mono" : (k == 0 ? "left " : "right"), peak,
               db(peak), db(frames ? sqrt(sum / frames) : 0), clipped);
    }

    // Per second: RMS and rising zero crossings per second of each channel.
    for(long start = 0; c.freq > 0 && start + c.freq <= frames; start += c.freq)
    {
        printf("    %2lds:", start / c.freq);

        for(int k = 0; k < ch; k++)
        {
            double sum = 0;
            long rising = 0;
            int prev = c.samples[(size_t)start * ch + k];

            for(long i = start; i < start + c.freq; i++)
            {
                int s = c.samples[(size_t)i * ch + k];
                sum += (double)s * s;

                if(prev < 0 && s >= 0)
                    rising++;

                prev = s;
            }

            printf("  %s rms %6.1f dBFS ~%6ld Hz", ch == 1 ? "" : (k == 0 ? "L" : "R"),
                   db(sqrt(sum / c.freq)), rising);
        }

        printf("\n");
    }
}

// The track count in an NSF or GBS header, -1 for other files.
int header_tracks(const char *path)
{
    FILE *f = fopen(path, "rb");

    if(!f)
        return -1;

    unsigned char h[8];
    size_t n = fread(h, 1, sizeof(h), f);
    fclose(f);

    if(n >= 7 && memcmp(h, "NESM\x1A", 5) == 0)
        return h[6];

    if(n >= 5 && memcmp(h, "GBS", 3) == 0)
        return h[4];

    return -1;
}

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

const char *state_name(int playing)
{
    return playing == ZCM_PLAYING ? "playing" : playing == ZCM_PAUSED ? "paused" : "stopped";
}

// Runs the game-side polling (once a frame) for 'ms' milliseconds.
void play_for(int ms)
{
    const std::chrono::steady_clock::time_point end =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);

    while(std::chrono::steady_clock::now() < end)
    {
        zcmusic_poll();
        rest(16);
    }
}

int run_file(const std::string &out_dir, const char *path, int seconds, int track)
{
    printf("%s\n", path);
    std::vector<char> name(path, path + strlen(path) + 1);
    ZCMUSIC *m = (ZCMUSIC *)zcmusic_load_file(&name[0]);

    if(!m)
    {
        printf("  zcmusic_load_file: failed\n\n");
        return 1;
    }

    printf("  loaded: type %d, filename \"%s\"\n", m->type, m->filename);
    int tracks = zcmusic_get_tracks(m);
    int head = header_tracks(path);

    if(head >= 0)
        printf("  zcmusic_get_tracks: %d (header: %d tracks, so %d expected)\n", tracks, head, head > 1 ? head : 0);
    else
        printf("  zcmusic_get_tracks: %d\n", tracks);

    {
        std::lock_guard<std::mutex> guard(g_capture_lock);
        g_capture = Capture();
        g_capture.music = m;
    }

    bool ok = zcmusic_play(m, 255);
    printf("  zcmusic_play: %s, state %s\n", ok ? "ok" : "failed", state_name(m->playing));

    if(track >= 0)
    {
        int result = zcmusic_change_track(m, track);
        printf("  zcmusic_change_track(%d): %d, ZCMUSIC::track %d\n", track, result, m->track);
    }

    const int half = seconds * 500;
    play_for(half);
    zcmusic_pause(m, ZCM_PAUSE);
    int pos = m->position;
    play_for(500);
    printf("  paused 0.5 s: state %s, position %d -> %d\n", state_name(m->playing), pos, m->position);
    zcmusic_pause(m, ZCM_RESUME);
    play_for(seconds * 1000 - half);
    printf("  after %d s: state %s, position %d\n", seconds, state_name(m->playing), m->position);

    zcmusic_stop(m);
    printf("  zcmusic_stop: state %s\n", state_name(m->playing));
    zcmusic_unload_file(m);

    Capture c;
    {
        std::lock_guard<std::mutex> guard(g_capture_lock);
        c = g_capture;
        g_capture = Capture();
    }

    if(c.samples.empty())
    {
        printf("  nothing reached the stream\n\n");
        return 1;
    }

    std::string wav = out_dir + "/" + base_name(path) + ".wav";
    printf("  %s %s\n", write_wav(wav, c) ? "wrote" : "could not write", wav.c_str());
    analyse(c);
    printf("\n");
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if(argc < 3)
    {
        fprintf(stderr, "usage: zcmusic_smoke <out-dir> [-seconds N] [-bufsz KB] [-track N] <file>...\n");
        return 2;
    }

    std::string out_dir = argv[1];
    int seconds = 6;
    int track = -1;
    std::vector<const char *> files;

    for(int i = 2; i < argc; i++)
    {
        if(strcmp(argv[i], "-seconds") == 0 && i + 1 < argc)
            seconds = atoi(argv[++i]);
        else if(strcmp(argv[i], "-bufsz") == 0 && i + 1 < argc)
            zcmusic_bufsz = atoi(argv[++i]);
        else if(strcmp(argv[i], "-track") == 0 && i + 1 < argc)
            track = atoi(argv[++i]);
        else
            files.push_back(argv[i]);
    }

    if(seconds < 1)
        seconds = 1;

    if(allegro_init() != 0 || install_timer() != 0
       || install_sound(DIGI_AUTODETECT, MIDI_NONE, NULL) != 0)
    {
        fprintf(stderr, "zcport initialisation failed\n");
        return 2;
    }

    printf("sound: %s, zcmusic_bufsz %d KB\n\n", digi_driver->name, zcmusic_bufsz);
    zcmusic_port_buffer_hook = capture_buffer;
    zcmusic_init();
    int failed = 0;

    for(size_t i = 0; i < files.size(); i++)
        failed += run_file(out_dir, files[i], seconds, track);

    zcmusic_exit();
    zcmusic_port_buffer_hook = NULL;
    printf("%d of %d files failed\n", failed, (int)files.size());
    return failed ? 1 : 0;
}

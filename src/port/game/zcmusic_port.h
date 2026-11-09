// zplayer: what src/port/game/zcmusic_port.cpp offers beyond the zcmusic API
// (src/zcmusic.h): for tools such as src/port/tools/zcmusic_smoke.cpp, and for
// music of 2.55 and 3.0 quests (src/port/game/q30_audio.cpp).

#ifndef ZCMUSIC_PORT_H
#define ZCMUSIC_PORT_H

#include "zcmusic.h"

#ifdef __cplusplus
extern "C"
{
#endif

// When set, called with every audio stream buffer zcmusic has just filled,
// before it goes back to the stream (free_audio_stream_buffer()): 'frames'
// frames of unsigned 16-bit samples, two per frame when 'stereo', played at
// 'freq' Hz. Called from zcmusic_poll() (the game thread or the 25 ms timer)
// with zcmusic's lock held.
typedef void (*zcmusic_port_buffer_hook_t)(const ZCMUSIC *music, const void *data, int frames, int stereo, int freq);
extern zcmusic_port_buffer_hook_t zcmusic_port_buffer_hook;

// Upstream's zcmusic functions for 2.55 and 3.0 music. Positions and lengths are
// seconds * 10000, as ZScript has them; -10000 when there is no stream.
// OGG, MP3 and modules; GME files have no position, speed or loop points.

// Loads like zcmusic_load_file(), but reads an OGG or MP3 whole, from its start (a 2.53
// quest's MP3 starts a block in), so position, loop points and length work at once.
ZCMUSIC *zcmusic_port_load_whole(const char *filename);

// Sets the voice volume of a playing file. A module also stops scaling its own output,
// so call it right after the first zcmusic_play() with volume 255.
bool zcmusic_set_volume(ZCMUSIC *zcm, int vol);
int zcmusic_get_curpos(ZCMUSIC *zcm);
void zcmusic_set_curpos(ZCMUSIC *zcm, int value);
void zcmusic_set_speed(ZCMUSIC *zcm, int value);        // 10000 is normal
int zcmusic_get_length(ZCMUSIC *zcm);
void zcmusic_set_loop(ZCMUSIC *zcm, double start, double end);  // seconds; 0,0 changes nothing

// What upstream keeps in ZCMUSIC. fadevolume is 0-10000, 10000 unless a fade set it;
// fadeoutframes is how long this track fades when replaced.
int zcmusic_port_fadevolume(const ZCMUSIC *zcm);
void zcmusic_port_set_fadevolume(ZCMUSIC *zcm, int v);
int zcmusic_port_fadeoutframes(const ZCMUSIC *zcm);
void zcmusic_port_set_fadeoutframes(ZCMUSIC *zcm, int v);

// A whole OGG file to signed 16-bit interleaved samples. *pcm is malloc'd.
bool zcmusic_port_decode_ogg_sample(const void *data, long len, short **pcm, int *channels, int *rate, int *frames);

#ifdef __cplusplus
}
#endif

#endif // ZCMUSIC_PORT_H

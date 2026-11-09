// zcport SDL platform: MIDI through TinySoundFont, for systems with no system
// synthesizer (Android and Linux; also built, without SDL, for the Nintendo
// Switch platform, platform_switch.cpp).
//
// Plays the channel messages of the MIDI player (src/port/al/platform_sound.cpp)
// on the General MIDI soundfont "soundfont.sf2" in the game folder. Without
// that file open() fails and the music is silent, as on a system with no
// backend; the player's timing is the same either way. The APK bundles
// GeneralUser GS by S. Christian Collins as that file.
//
// short_msg/long_msg/reset come from the game or timer thread, one at a time,
// and render() from the audio thread, so messages wait in a single-producer,
// single-consumer ring and are applied at the start of the next audio block.
// Nothing here allocates on the audio thread: the voices and channels are set
// up in open().
//
// Written from scratch for zcport (no Allegro code).

#if !defined(__SWITCH__)
#include "platform_sdl_internal.h"
#endif
#include "zcport/midi_backend.h"

#if defined(__ANDROID__) || defined(__linux__) || defined(__SWITCH__)

#if defined(__SWITCH__)
#include <stdio.h>
namespace zcport_sdl { const ZCPORT_MIDI_BACKEND *midi_backend(); }
#define SDL_Log(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#else
#include <SDL3/SDL.h>
#endif

#include <atomic>
#include <cstring>

#define TSF_IMPLEMENTATION
#include "tsf.h"

namespace
{

const char *const kSoundfont = "soundfont.sf2";
const int kChannels = 16;
const int kDrumChannel = 9;
const int kMaxVoices = 128;
const int kDefaultRate = 44100;
const float kGainDb = 3.0f;
const int kBlockFrames = 2048;

const unsigned kRingSize = 8192;           // a power of two
const unsigned kResetMessage = 0xFFFFFFFFu;

tsf *g_synth = NULL;
int g_rate = 0;                            // audio thread, after open()

unsigned g_ring[kRingSize];
std::atomic<unsigned> g_head(0);           // written by the sender
std::atomic<unsigned> g_tail(0);           // written by the audio thread

float g_block[kBlockFrames * 2];           // audio thread

void push_message(unsigned msg)
{
   const unsigned head = g_head.load(std::memory_order_relaxed);
   const unsigned next = (head + 1) & (kRingSize - 1);
   if (next == g_tail.load(std::memory_order_acquire))
      return;                              // full: the message is dropped
   g_ring[head] = msg;
   g_head.store(next, std::memory_order_release);
}

void reset_channels()
{
   for (int c = 0; c < kChannels; c++) {
      tsf_channel_sounds_off_all(g_synth, c);
      tsf_channel_midi_control(g_synth, c, 121, 0);   // reset all controllers
      tsf_channel_set_presetnumber(g_synth, c, 0, c == kDrumChannel);
   }
}

void apply_message(unsigned msg)
{
   if (msg == kResetMessage) {
      reset_channels();
      return;
   }
   const int channel = (int)(msg & 0x0F);
   const int data1 = (int)((msg >> 8) & 0x7F);
   const int data2 = (int)((msg >> 16) & 0x7F);
   switch (msg & 0xF0) {
   case 0x80:
      tsf_channel_note_off(g_synth, channel, data1);
      break;
   case 0x90:
      if (data2)
         tsf_channel_note_on(g_synth, channel, data1, (float)data2 / 127.0f);
      else
         tsf_channel_note_off(g_synth, channel, data1);
      break;
   case 0xB0:
      tsf_channel_midi_control(g_synth, channel, data1, data2);
      break;
   case 0xC0:
      tsf_channel_set_presetnumber(g_synth, channel, data1, channel == kDrumChannel);
      break;
   case 0xE0:
      tsf_channel_set_pitchwheel(g_synth, channel, data1 | (data2 << 7));
      break;
   default:
      break;
   }
}

int synth_open(void)
{
   if (g_synth)
      return 0;
   tsf *f = tsf_load_filename(kSoundfont);
   if (!f) {
      SDL_Log("zplayer: cannot load %s, MIDI music is silent", kSoundfont);
      return -1;
   }
   g_rate = kDefaultRate;
   tsf_set_output(f, TSF_STEREO_INTERLEAVED, g_rate, kGainDb);
   tsf_set_max_voices(f, kMaxVoices);
   g_synth = f;
   g_head.store(0);
   g_tail.store(0);
   reset_channels();
   SDL_Log("zplayer: MIDI synthesizer ready (%d presets)", tsf_get_presetcount(f));
   return 0;
}

// The digital output is already closed: render() is no longer called.
void synth_close(void)
{
   if (g_synth) {
      tsf_close(g_synth);
      g_synth = NULL;
   }
}

void synth_short_msg(unsigned int msg)
{
   if (g_synth && msg != kResetMessage)
      push_message(msg);
}

// The only system exclusive messages that matter here are the "reset to
// defaults" ones (GM, GS, XG); the others change nothing this synthesizer has.
void synth_long_msg(const unsigned char *data, int length)
{
   static const unsigned char kGmOn[] = { 0xF0, 0x7E, 0x7F, 0x09 };
   static const unsigned char kGsReset[] = { 0x42, 0x12, 0x40, 0x00, 0x7F };
   static const unsigned char kXgOn[] = { 0x4C, 0x00, 0x00, 0x7E };
   if (!g_synth || !data || length < 6)
      return;
   const bool gm = memcmp(data, kGmOn, sizeof(kGmOn)) == 0;
   const bool gs = data[1] == 0x41 && length >= 9
                   && memcmp(data + 3, kGsReset, sizeof(kGsReset)) == 0;
   const bool xg = data[1] == 0x43 && length >= 8
                   && memcmp(data + 3, kXgOn, sizeof(kXgOn)) == 0;
   if (gm || gs || xg)
      push_message(kResetMessage);
}

void synth_reset(void)
{
   if (g_synth)
      push_message(kResetMessage);
}

void synth_render(float *mix, int frames, int freq)
{
   if (!g_synth || frames <= 0)
      return;

   unsigned tail = g_tail.load(std::memory_order_relaxed);
   const unsigned head = g_head.load(std::memory_order_acquire);
   while (tail != head) {
      apply_message(g_ring[tail]);
      tail = (tail + 1) & (kRingSize - 1);
   }
   g_tail.store(tail, std::memory_order_release);

   if (freq != g_rate) {
      g_rate = freq;
      tsf_set_output(g_synth, TSF_STEREO_INTERLEAVED, g_rate, kGainDb);
   }

   while (frames > 0) {
      const int n = frames < kBlockFrames ? frames : kBlockFrames;
      tsf_render_float(g_synth, g_block, n, 0);
      for (int i = 0; i < n * 2; i++)
         mix[i] += g_block[i] * 32768.0f;
      mix += n * 2;
      frames -= n;
   }
}

const ZCPORT_MIDI_BACKEND g_tsf_backend =
{
   "TinySoundFont",
   synth_open,
   synth_close,
   synth_short_msg,
   synth_long_msg,
   synth_reset,
   synth_render
};

} // namespace

const ZCPORT_MIDI_BACKEND *zcport_sdl::midi_backend()
{
   return &g_tsf_backend;
}

#endif // Android, Linux or Switch

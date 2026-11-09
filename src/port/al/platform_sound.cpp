// zcport platform module: the sound API.
//
// Everything the game can read back keeps the reference build's behaviour
// (measured with tests/conformance/test_platform_sound_sdl.cpp against
// Allegro 4.4.2 with its DirectSound "Allegmix" driver and the Windows MIDI
// mapper), whether or not a sound device is open:
//   - voices: 256 virtual voices, 8 "physical" ones (the reference driver's
//     voice count); a voice without a physical voice reads -1 everywhere;
//     the oldest voice loses its physical voice when a new one needs it.
//     A playing voice's position advances in real time at its frequency,
//     loops or ends (then reads -1); stopped voices read -1 but keep their
//     position; play_sample() voices are freed when they end (voice_check()
//     gives NULL) but keep their slot until it is needed.
//   - the MIDI player runs the file's events in real time (tempo changes
//     included) on a timer slot of its own: midi_pos is the beat number of
//     the last event played (beat 1 = the first), -(beats + 1) after the end,
//     -1 after stop_midi(); looping, loop points, pause/resume; midi_seek()
//     starts again from the beginning for a target at or before midi_pos,
//     plays the event it reaches at the player's next tick and un-pauses.
//   - audio streams: a ring of 2 x bufcount buffers, all handed out at once
//     at the start, then half a ring each time the voice has moved into the
//     other half.
//   - set_volume() / get_volume(): -1 until set, clamped to 255, negative
//     values leave a volume unchanged.
//
// That model is the only source of truth. Two outputs follow it:
//   - the digital mixer (audio thread, through the platform's audio_open
//     hook): every change the API makes to a voice is published as a
//     timestamped snapshot, and the mixer plays the snapshots on the same
//     clock, a fixed delay behind (see "digital mixer");
//   - the MIDI output (zcport/midi_backend.h): the player's events become
//     the channel messages the reference build sends its Windows MIDI driver
//     (see "MIDI output").
//
// destroy_sample() and destroy_midi() (files module) must call
// zcport_sound_forget_sample() / zcport_sound_forget_midi()
// (zcport/platform_sound.h).
//
// Written from scratch for zcport (no Allegro code).

#include "platform_internal.h"
#include "zcport/midi_backend.h"
#include "zcport/platform_sound.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// drivers and globals
// ---------------------------------------------------------------------------

static DIGI_DRIVER make_digi(int id, const char *name, int voices)
{
   DIGI_DRIVER d;
   memset(&d, 0, sizeof(d));
   d.id = id;
   d.name = d.desc = d.ascii_name = name;
   d.voices = voices;
   d.max_voices = voices;
   d.def_voices = voices;
   return d;
}

static MIDI_DRIVER make_midi(int id, const char *name)
{
   MIDI_DRIVER d;
   memset(&d, 0, sizeof(d));
   d.id = id;
   d.name = d.desc = d.ascii_name = name;
   d.xmin = d.xmax = -1;
   return d;
}

static const char kDigiName[] = "zcport mixer";
static const char kDigiSilentName[] = "zcport mixer (silent)";
static const char kMidiSilentName[] = "zcport MIDI (silent)";

static DIGI_DRIVER digi_none_drv = make_digi(DIGI_NONE, "No sound", 0);
static DIGI_DRIVER digi_zcport_drv = make_digi(DIGI_ZCPORT, kDigiSilentName, 8);
static MIDI_DRIVER midi_none_drv = make_midi(MIDI_NONE, "No MIDI");
static MIDI_DRIVER midi_zcport_drv = make_midi(MIDI_ZCPORT, kMidiSilentName);

MIDI_DRIVER midi_digmid = make_midi(MIDI_DIGMID, "DIGMID");

DIGI_DRIVER *digi_driver = &digi_none_drv;
DIGI_DRIVER *digi_input_driver = &digi_none_drv;
int digi_card = DIGI_AUTODETECT;
int digi_input_card = DIGI_AUTODETECT;
MIDI_DRIVER *midi_driver = &midi_none_drv;
MIDI_DRIVER *midi_input_driver = &midi_none_drv;
int midi_card = MIDI_AUTODETECT;
int midi_input_card = MIDI_AUTODETECT;

volatile long midi_pos = -1;
volatile long midi_time = 0;
long midi_loop_start = -1;
long midi_loop_end = -1;

void (*midi_msg_callback)(int msg, int byte1, int byte2) = NULL;
void (*midi_meta_callback)(int type, const unsigned char *data, int length) = NULL;
void (*midi_sysex_callback)(const unsigned char *data, int length) = NULL;
void (*midi_recorder)(unsigned char data) = NULL;
void (*digi_recorder)(void) = NULL;

namespace
{

std::recursive_mutex g_lock;
bool g_installed = false;
int g_digi_volume = -1;
int g_midi_volume = -1;

// The registered MIDI backend (zcport_set_midi_backend); constant-initialised,
// so registering from a static constructor is safe.
const ZCPORT_MIDI_BACKEND *g_midi_backend = NULL;

uint64_t now_us()
{
   static const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
   return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - t0).count();
}

void set_name(DIGI_DRIVER &d, const char *name)
{
   d.name = d.desc = d.ascii_name = name;
}

void set_name(MIDI_DRIVER &d, const char *name)
{
   d.name = d.desc = d.ascii_name = name;
}

// ---------------------------------------------------------------------------
// voices
// ---------------------------------------------------------------------------

const int kVoices = 256;
const int kPhysical = 8;

struct Voice
{
   bool used;
   const SAMPLE *spl;
   bool autokill;
   int phys;              // physical voice or -1
   unsigned long serial;  // allocation order (oldest loses its physical voice)
   bool playing;
   bool ended;            // played to the end (not looping)
   double pos;            // position (samples) at t0
   uint64_t t0;
   int freq, vol, pan, playmode, priority;
   unsigned long len, loop_start, loop_end;
   // the sample's data and format when the voice was set up (for the mixer)
   const void *data;
   int bits, stereo;
};

Voice g_voice[kVoices];
int g_phys_owner[kPhysical];
unsigned long g_serial = 0;
bool g_phys_ready = false;

// Hands the voice's current state to the mixer (defined below). 'reanchor'
// marks a jump of the position (set up, voice_set_position()).
void publish(int v, bool reanchor = false);

void init_voices()
{
   if (!g_phys_ready) {
      for (int i = 0; i < kPhysical; i++)
         g_phys_owner[i] = -1;
      g_phys_ready = true;
   }
}

bool valid(int v)
{
   return v >= 0 && v < kVoices && g_voice[v].used;
}

// Brings pos / t0 up to now; ends or wraps the voice.
void update(Voice &vc)
{
   if (!vc.playing)
      return;
   uint64_t now = now_us();
   double p = vc.pos + (double)(now - vc.t0) * vc.freq / 1000000.0;
   vc.t0 = now;
   if (vc.playmode & PLAYMODE_LOOP) {
      double ls = (double)vc.loop_start, le = (double)vc.loop_end;
      if (le <= ls)
         le = (double)vc.len, ls = 0;
      if (le > ls && p >= le)
         p = ls + fmod(p - ls, le - ls);
   }
   else if (p >= (double)vc.len) {
      p = (double)vc.len;
      vc.playing = false;
      vc.ended = true;
   }
   vc.pos = p;
}

void release_phys(Voice &vc)
{
   if (vc.phys >= 0) {
      g_phys_owner[vc.phys] = -1;
      vc.phys = -1;
   }
}

void free_voice(int v)
{
   Voice &vc = g_voice[v];
   release_phys(vc);
   vc.used = false;
   vc.spl = NULL;
   vc.playing = false;
   publish(v);
}

void grab_phys(int v)
{
   for (int i = 0; i < kPhysical; i++) {
      if (g_phys_owner[i] < 0) {
         g_phys_owner[i] = v;
         g_voice[v].phys = i;
         return;
      }
   }
   // Take the physical voice of the oldest voice.
   int best = -1;
   for (int i = 0; i < kPhysical; i++) {
      int o = g_phys_owner[i];
      if (best < 0 || g_voice[o].serial < g_voice[g_phys_owner[best]].serial)
         best = i;
   }
   if (best >= 0) {
      int o = g_phys_owner[best];
      Voice &old = g_voice[o];
      update(old);
      old.playing = false;
      old.phys = -1;
      g_phys_owner[best] = v;
      g_voice[v].phys = best;
      publish(o);
   }
}

void setup_voice(int v, const SAMPLE *spl)
{
   Voice &vc = g_voice[v];
   vc.used = true;
   vc.spl = spl;
   vc.autokill = false;
   vc.phys = -1;
   vc.serial = ++g_serial;
   vc.playing = false;
   vc.ended = false;
   vc.pos = 0;
   vc.t0 = now_us();
   vc.freq = spl->freq;
   vc.vol = 255;
   vc.pan = 128;
   vc.playmode = PLAYMODE_PLAY;
   vc.priority = spl->priority;
   vc.len = spl->len;
   vc.loop_start = spl->loop_start;
   vc.loop_end = spl->loop_end;
   vc.data = spl->data;
   vc.bits = spl->bits;
   vc.stereo = spl->stereo;
   grab_phys(v);
   publish(v, true);
}

int alloc_voice(const SAMPLE *spl)
{
   if (!g_installed || !spl || digi_driver == &digi_none_drv)
      return -1;
   init_voices();
   int v = -1;
   for (int i = 0; i < kVoices; i++) {
      if (!g_voice[i].used) {
         v = i;
         break;
      }
   }
   if (v < 0) {
      // Reuse a finished play_sample() voice.
      for (int i = 0; i < kVoices; i++) {
         Voice &vc = g_voice[i];
         update(vc);
         if (vc.autokill && !vc.playing) {
            free_voice(i);
            v = i;
            break;
         }
      }
   }
   if (v < 0)
      return -1;
   setup_voice(v, spl);
   return v;
}

// ---------------------------------------------------------------------------
// digital mixer (audio thread)
// ---------------------------------------------------------------------------
//
// The mixer never decides anything the game can read back. Each change the
// API makes to a voice is published with its time (now_us()) as a snapshot
// of the voice: anchor position and time, frequency, volume, pan, play mode,
// playing. The mixer applies the snapshots on the same clock, 'delay' behind
// (a little more than two device blocks), at the frame they belong to, and
// renders the frame for mixer time t at the position
//     pos + (t - t0) * freq / 1e6    (looped or ended exactly as update())
// So what is heard at any moment is what voice_get_position() reported
// 'delay' earlier: starts and stops land on the right sample, attacks are
// never cut, a sound is never cut before the end the game saw. The mixer
// clock advances one period per output frame and is slewed (at most 0.1 %)
// towards now_us() - delay, so the sound card's clock cannot drift away
// from the game's.
//
// Locking: the game thread changes the mixer under g_lock and then the mix
// lock (the platform's audio_lock, which excludes the mix callback); the
// callback only takes the mix lock (implicitly), never g_lock.
//
// PLAYMODE_BACKWARD and PLAYMODE_BIDIR change what is heard (the sample read
// backwards, ping-pong between the loop points); the position the game reads
// keeps counting forward as before.

const int kMixRate = 44100;          // what get_mixer_frequency() reports
const size_t kMaxQueue = 8192;       // events kept while the device stalls
const double kMinDelay = 15000.0;    // us
const double kMaxDelay = 250000.0;

struct MixVoice
{
   bool on;                     // allocated, has a physical voice, has data
   bool playing;
   const SAMPLE *spl;
   const unsigned char *data;
   int bits, stereo;
   unsigned long len, loop_start, loop_end;
   double pos;
   uint64_t t0;
   int freq, vol, pan, playmode;
};

struct MixEvent
{
   uint64_t t;
   int voice;
   bool reanchor;
   MixVoice st;                 // a voice's state, or (dst) only st.spl
   unsigned char *dst;          // audio stream data: copy 'bytes' here
   std::vector<unsigned char> bytes;
};

struct Mixer
{
   bool open;                   // written under g_lock + mix lock
   int rate;
   std::deque<MixEvent> queue;  // mix lock
   MixVoice cur[kVoices];       // what the mixer plays (mix lock)
   long long parity[kVoices];   // BIDIR direction at the anchor
   double clock;                // mixer time (us) of the next frame
   bool clock_ok;
   double delay;
   double max_block;
   int off_count;               // callbacks in a row far off the target
   std::vector<float> acc;
};

Mixer g_mix;
const ZCPORT_PLATFORM *g_mix_plat = NULL;   // platform whose device is open
std::mutex g_mix_mutex;                     // mix lock without audio_lock hooks
std::atomic<int> g_mix_digi(255);           // set_volume() digi, 0..255
std::atomic<int> g_mix_scale(1);            // set_volume_per_voice()
std::atomic<int> g_mix_quality(2);          // set_mixer_quality()
std::atomic<ZCPORT_SOUND_TAP> g_tap(NULL);
std::atomic<void *> g_tap_user(NULL);
// an open MIDI backend that renders through the mixer (software synth)
std::atomic<const ZCPORT_MIDI_BACKEND *> g_mix_synth(NULL);

bool platform_locks(const ZCPORT_PLATFORM *p)
{
   return p && p->audio_lock && p->audio_unlock;
}

// Excludes the mix callback (game side).
struct MixLock
{
   const ZCPORT_PLATFORM *p;
   MixLock() : p(platform_locks(g_mix_plat) ? g_mix_plat : NULL)
   {
      if (p)
         p->audio_lock();
      else
         g_mix_mutex.lock();
   }
   ~MixLock()
   {
      if (p)
         p->audio_unlock();
      else
         g_mix_mutex.unlock();
   }
};

void snapshot(const Voice &vc, MixVoice &m)
{
   m.on = vc.used && vc.phys >= 0 && vc.spl && vc.data && vc.len > 0 &&
          (vc.bits == 8 || vc.bits == 16);
   m.playing = m.on && vc.playing;
   m.spl = vc.spl;
   m.data = (const unsigned char *)vc.data;
   m.bits = vc.bits;
   m.stereo = vc.stereo ? 1 : 0;
   m.len = vc.len;
   m.loop_start = vc.loop_start;
   m.loop_end = vc.loop_end;
   m.pos = vc.pos;
   m.t0 = vc.t0;
   m.freq = vc.freq;
   m.vol = vc.vol;
   m.pan = vc.pan;
   m.playmode = vc.playmode;
}

void loop_bounds(const MixVoice &m, double *ls, double *le)
{
   *ls = (double)m.loop_start;
   *le = (double)m.loop_end;
   if (*le <= *ls) {
      *le = (double)m.len;
      *ls = 0;
   }
}

// Position of 'm' at mixer time t, wrapped as update() wraps it, and the
// number of wraps since the anchor. False once a non-looping voice ended.
bool logical_at(const MixVoice &m, double t, double *u, long long *wraps)
{
   double el = t - (double)m.t0;
   if (el < 0)
      el = 0;
   double p = m.pos + el * m.freq / 1000000.0;
   *wraps = 0;
   if (m.playmode & PLAYMODE_LOOP) {
      double ls, le;
      loop_bounds(m, &ls, &le);
      if (le > ls && p >= le) {
         double n = floor((p - ls) / (le - ls));
         p -= n * (le - ls);
         if (p >= le) {
            p -= le - ls;
            n += 1;
         }
         if (p < ls)
            p = ls;
         *wraps = (long long)n;
      }
   }
   else if (p >= (double)m.len)
      return false;
   *u = p;
   return true;
}

void apply_event(const MixEvent &e, double t)
{
   if (e.dst) {
      if (!e.bytes.empty())
         memcpy(e.dst, &e.bytes[0], e.bytes.size());
      return;
   }
   MixVoice &m = g_mix.cur[e.voice];
   long long par = 0;
   double u;
   long long w = 0;
   if (!e.reanchor && m.on) {
      logical_at(m, t, &u, &w);
      par = g_mix.parity[e.voice] + w;
   }
   w = 0;
   logical_at(e.st, t, &u, &w);
   g_mix.parity[e.voice] = par - w;
   m = e.st;
}

void push_event(MixEvent &e);

void publish(int v, bool reanchor)
{
   if (!g_mix.open || v < 0 || v >= kVoices)
      return;
   MixEvent e;
   e.t = now_us();
   e.voice = v;
   e.reanchor = reanchor;
   snapshot(g_voice[v], e.st);
   e.dst = NULL;
   push_event(e);
}

void push_event(MixEvent &e)
{
   MixLock lock;
   while (g_mix.queue.size() >= kMaxQueue) {
      apply_event(g_mix.queue.front(), (double)g_mix.queue.front().t);
      g_mix.queue.pop_front();
   }
   g_mix.queue.push_back(MixEvent());
   std::swap(g_mix.queue.back(), e);
}

// An audio stream buffer the game gave back: the mixer's copy of the ring
// ('dst') gets it at mixer time t.
void publish_data(const SAMPLE *spl, uint64_t t, unsigned char *dst, const unsigned char *src, size_t n)
{
   if (!g_mix.open || !dst || !n)
      return;
   MixEvent e;
   e.t = t;
   e.voice = 0;
   e.reanchor = false;
   memset(&e.st, 0, sizeof(e.st));
   e.st.spl = spl;
   e.dst = dst;
   e.bytes.assign(src, src + n);
   push_event(e);
}

// Drops every reference to 'spl' at once (it is about to be freed).
void mix_forget(const SAMPLE *spl)
{
   if (!g_mix.open || !spl)
      return;
   MixLock lock;
   for (std::deque<MixEvent>::iterator it = g_mix.queue.begin(); it != g_mix.queue.end();) {
      if (it->st.spl == spl)
         it = g_mix.queue.erase(it);
      else
         ++it;
   }
   for (int v = 0; v < kVoices; v++) {
      if (g_mix.cur[v].spl == spl) {
         g_mix.cur[v].on = false;
         g_mix.cur[v].playing = false;
         g_mix.cur[v].spl = NULL;
         g_mix.cur[v].data = NULL;
      }
   }
}

inline float sample_at(const MixVoice &m, long i, int ch)
{
   if (i < 0)
      i = 0;
   else if (i >= (long)m.len)
      i = (long)m.len - 1;
   size_t at = (size_t)i * (m.stereo ? 2 : 1) + ch;
   if (m.bits == 8)
      return (float)(((int)m.data[at] - 128) * 256);
   return (float)((int)((const unsigned short *)m.data)[at] - 32768);
}

// Adds voice 'v' to frames [k0, k1) of g_mix.acc; frame k is mixer time
// t_start + k * step (us).
void render_voice(int v, int k0, int k1, double t_start, double step)
{
   const MixVoice &m = g_mix.cur[v];
   if (!m.on || !m.playing || m.freq <= 0 || k0 >= k1)
      return;
   int vol = m.vol < 0 ? 0 : (m.vol > 255 ? 255 : m.vol);
   int pan = m.pan < 0 ? 0 : (m.pan > 255 ? 255 : m.pan);
   int scale = g_mix_scale.load();
   if (scale < 0)
      scale = 2;   // "chosen from the voice count": 8 voices
   float base = (float)vol / 255.0f * (float)g_mix_digi.load() / 255.0f *
                (float)ldexp(1.0, 1 - scale);
   float gl = base * (float)(255 - pan) / 255.0f;
   float gr = base * (float)pan / 255.0f;
   if (gl == 0.0f && gr == 0.0f)
      return;

   double u;
   long long wraps;
   if (!logical_at(m, t_start + k0 * step, &u, &wraps))
      return;
   long long par = g_mix.parity[v] + wraps;
   const bool loop = (m.playmode & PLAYMODE_LOOP) != 0;
   const bool back = (m.playmode & PLAYMODE_BACKWARD) != 0;
   const bool bidir = loop && (m.playmode & PLAYMODE_BIDIR);
   double ls, le;
   loop_bounds(m, &ls, &le);
   const bool can_wrap = loop && le > ls;
   const long ls_i = (long)ls, le_i = (long)le;
   const bool edges = can_wrap && le_i > ls_i;
   const double inc = (double)m.freq * step / 1000000.0;
   const double len = (double)m.len;
   const bool interp = g_mix_quality.load() >= 2;
   float *acc = &g_mix.acc[0];

   for (int k = k0; k < k1; k++) {
      bool rev = back != (bidir && (par & 1));
      double a = rev ? (loop ? ls + le - u : len - u) : u;
      if (a < 0)
         a = 0;
      long i0 = (long)a;
      const float f = (float)(a - (double)i0);
      long i1 = i0 + 1;
      // The sample after the loop end: the loop start for a loop (forwards
      // or backwards), the end itself for ping-pong.
      if (edges) {
         if (bidir) {
            if (i0 >= le_i)
               i0 = le_i - 1;
            if (i1 >= le_i)
               i1 = le_i - 1;
         }
         else if (i0 == le_i - 1)
            i1 = ls_i;
         else if (i0 == le_i) {
            i0 = ls_i;
            i1 = ls_i + 1 < le_i ? ls_i + 1 : ls_i;
         }
      }
      float l, r;
      if (interp) {
         float l0 = sample_at(m, i0, 0), l1 = sample_at(m, i1, 0);
         l = l0 + (l1 - l0) * f;
         if (m.stereo) {
            float r0 = sample_at(m, i0, 1), r1 = sample_at(m, i1, 1);
            r = r0 + (r1 - r0) * f;
         }
         else
            r = l;
      }
      else {
         l = sample_at(m, i0, 0);
         r = m.stereo ? sample_at(m, i0, 1) : l;
      }
      acc[2 * k] += l * gl;
      acc[2 * k + 1] += r * gr;

      u += inc;
      if (can_wrap) {
         if (u >= le) {
            double n = floor((u - ls) / (le - ls));
            u -= n * (le - ls);
            if (u < ls)
               u = ls;
            par += (long long)n;
         }
      }
      else if (u >= len)
         break;
   }
}

// Produces 'frames' stereo frames (mix lock held).
void mix_block(short *out, int frames)
{
   if (frames <= 0)
      return;
   if (!g_mix.open) {
      memset(out, 0, (size_t)frames * 4);
      return;
   }
   const double nominal = 1000000.0 / (double)g_mix.rate;
   const double block = frames * nominal;
   if (block > g_mix.max_block) {
      g_mix.max_block = block;
      double d = 2.0 * block + 5000.0;
      g_mix.delay = d < kMinDelay ? kMinDelay : (d > kMaxDelay ? kMaxDelay : d);
   }
   // Callback times jitter by a millisecond or so; drift between the clocks
   // is slow (ppm): a gentle slew follows it. An offset that persists (the
   // device filling its buffer at start, a stall) is taken at once.
   const double target = (double)now_us() - g_mix.delay;
   double err = target - g_mix.clock;
   const double persistent = block * 0.5 > 4000.0 ? block * 0.5 : 4000.0;
   if (fabs(err) > persistent)
      g_mix.off_count++;
   else
      g_mix.off_count = 0;
   if (!g_mix.clock_ok || fabs(err) > 3.0 * g_mix.max_block + 20000.0 || g_mix.off_count >= 8) {
      g_mix.clock = target;
      g_mix.clock_ok = true;
      g_mix.off_count = 0;
      err = 0;
   }
   double corr = err * 0.005, lim = block * 0.001;
   if (corr > lim)
      corr = lim;
   else if (corr < -lim)
      corr = -lim;
   const double step = (block + corr) / frames;
   const double t_start = g_mix.clock;

   g_mix.acc.assign((size_t)frames * 2, 0.0f);
   int k = 0;
   while (k < frames) {
      int k_end = frames;
      while (!g_mix.queue.empty()) {
         const MixEvent &e = g_mix.queue.front();
         double fe = ((double)e.t - t_start) / step;
         if (fe >= (double)frames)
            break;                        // a later block
         int ke = fe <= (double)k ? k : (int)ceil(fe);
         if (ke >= frames)
            break;
         if (ke > k) {
            k_end = ke;
            break;
         }
         apply_event(e, t_start + k * step);
         g_mix.queue.pop_front();
      }
      for (int v = 0; v < kVoices; v++)
         if (g_mix.cur[v].playing)
            render_voice(v, k, k_end, t_start, step);
      k = k_end;
   }
   g_mix.clock = t_start + frames * step;

   const ZCPORT_MIDI_BACKEND *synth = g_mix_synth.load();
   if (synth)
      synth->render(&g_mix.acc[0], frames, g_mix.rate);

   const float *acc = &g_mix.acc[0];
   for (int i = 0; i < frames * 2; i++) {
      float s = acc[i];
      out[i] = s >= 32767.0f ? (short)32767 : (s <= -32768.0f ? (short)-32768 : (short)lrintf(s));
   }
}

// The platform's audio thread.
void mix_callback(void *user, void *buffer, int bytes)
{
   (void)user;
   if (bytes <= 0)
      return;
   int frames = bytes / 4;
   bool own = !platform_locks(g_mix_plat);
   if (own)
      g_mix_mutex.lock();
   mix_block((short *)buffer, frames);
   if (own)
      g_mix_mutex.unlock();
   if (bytes % 4)
      memset((char *)buffer + (size_t)frames * 4, 0, (size_t)(bytes % 4));
   ZCPORT_SOUND_TAP tap = g_tap.load();
   if (tap)
      tap(g_tap_user.load(), (const short *)buffer, frames);
}

// install_sound(): opens the platform's audio device (g_lock held).
bool open_output()
{
   const ZCPORT_PLATFORM *p = zcport_get_platform();
   if (!p || !p->audio_open)
      return false;
   g_mix.queue.clear();
   for (int v = 0; v < kVoices; v++) {
      memset(&g_mix.cur[v], 0, sizeof(g_mix.cur[v]));
      g_mix.parity[v] = 0;
   }
   g_mix.rate = kMixRate;
   g_mix.clock = 0;
   g_mix.clock_ok = false;
   g_mix.delay = kMinDelay;
   g_mix.max_block = 0;
   g_mix.off_count = 0;
   g_mix.acc.reserve(16384);
   g_mix_plat = p;
   int freq = 0;
   if (p->audio_open(kMixRate, 2, 16, mix_callback, NULL, &freq) != 0) {
      g_mix_plat = NULL;
      return false;
   }
   MixLock lock;
   g_mix.rate = freq > 0 ? freq : kMixRate;
   g_mix.open = true;
   return true;
}

// remove_sound() (g_lock held).
void close_output()
{
   if (!g_mix_plat)
      return;
   {
      MixLock lock;
      g_mix.open = false;
      g_mix.queue.clear();
      for (int v = 0; v < kVoices; v++)
         g_mix.cur[v].on = g_mix.cur[v].playing = false;
   }
   if (g_mix_plat->audio_close)
      g_mix_plat->audio_close();
   g_mix_plat = NULL;
}

// ---------------------------------------------------------------------------
// MIDI player
// ---------------------------------------------------------------------------

struct MidiEvent
{
   uint32_t tick;
   int tempo;   // microseconds per quarter note, or -1
   // channel message (status 0x80..0xEF), status 0 for anything else
   unsigned char status, d1, d2;
};

struct MidiPlayer
{
   MIDI *midi;
   int divisions;
   std::vector<MidiEvent> events;   // every event of every track, by time
   uint32_t end_tick;
   size_t cursor;
   uint32_t tick;
   double song_us;
   int tempo;
   bool playing, paused, looping;
   uint64_t next_fire;
   uint64_t last_wait;     // the period midi_resume() restarts the timer with
   uint64_t period;        // the period the timer runs with now
   bool timer_on;
};

MidiPlayer g_mp;

uint32_t read_vlq(const unsigned char *d, int len, int *i)
{
   uint32_t v = 0;
   for (int n = 0; n < 4 && *i < len; n++) {
      unsigned char c = d[(*i)++];
      v = (v << 7) | (c & 0x7F);
      if (!(c & 0x80))
         break;
   }
   return v;
}

void parse_track(const unsigned char *d, int len, std::vector<MidiEvent> &out, uint32_t *end)
{
   int i = 0;
   uint32_t t = 0;
   unsigned char status = 0;
   while (i < len) {
      t += read_vlq(d, len, &i);
      if (i >= len)
         break;
      unsigned char c = d[i];
      MidiEvent e;
      e.tick = t;
      e.tempo = -1;
      e.status = e.d1 = e.d2 = 0;
      if (c == 0xFF) {
         i++;
         if (i >= len)
            break;
         unsigned char type = d[i++];
         uint32_t n = read_vlq(d, len, &i);
         if (type == 0x51 && n >= 3 && i + 2 < len)
            e.tempo = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
         i += (int)n;
         out.push_back(e);
         if (type == 0x2F)
            break;
         continue;
      }
      if (c == 0xF0 || c == 0xF7) {
         i++;
         uint32_t n = read_vlq(d, len, &i);
         i += (int)n;
         out.push_back(e);
         continue;
      }
      if (c & 0x80) {
         status = c;
         i++;
      }
      int hi = status & 0xF0;
      int bytes = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
      if (status >= 0x80 && status < 0xF0) {
         e.status = status;
         e.d1 = (unsigned char)(i < len ? d[i] & 0x7F : 0);
         e.d2 = (unsigned char)(bytes == 2 && i + 1 < len ? d[i + 1] & 0x7F : 0);
      }
      i += bytes;
      out.push_back(e);
   }
   if (t > *end)
      *end = t;
}

bool load_events(MIDI *midi)
{
   g_mp.events.clear();
   g_mp.end_tick = 0;
   g_mp.divisions = midi->divisions > 0 ? midi->divisions : 96;
   std::vector<std::vector<MidiEvent> > tracks;
   for (int t = 0; t < MIDI_TRACKS; t++) {
      if (!midi->track[t].data || midi->track[t].len <= 0)
         continue;
      tracks.push_back(std::vector<MidiEvent>());
      parse_track(midi->track[t].data, midi->track[t].len, tracks.back(), &g_mp.end_tick);
   }
   // Merge by time, earlier tracks first on equal times.
   std::vector<size_t> at(tracks.size(), 0);
   for (;;) {
      int best = -1;
      for (size_t k = 0; k < tracks.size(); k++) {
         if (at[k] >= tracks[k].size())
            continue;
         if (best < 0 || tracks[k][at[k]].tick < tracks[best][at[best]].tick)
            best = (int)k;
      }
      if (best < 0)
         break;
      g_mp.events.push_back(tracks[best][at[best]++]);
   }
   return true;
}

// ---------------------------------------------------------------------------
// MIDI output
// ---------------------------------------------------------------------------
//
// The messages follow what the reference build's MIDI player hands the
// Windows MIDI mapper (recorded from its driver's raw output with notes,
// controllers, set_volume(), stop, pause, seek and loops):
//   - the MIDI volume scales controller 7: the song's channel volume v is
//     sent as v * midi_volume / 256 (unscaled while the volume is unset);
//     a set_volume() change is sent for every channel at the player's next
//     step;
//   - controller 7 and pitch bend events only update the channel; at the
//     end of each player step every channel whose volume (or the MIDI
//     volume) changed gets controller 7, then one whose bend changed gets the
//     bend; a program change is followed by controller 7 with the channel's
//     current (sent) volume;
//   - other controllers, notes and program changes go out as they come; a
//     note-off's velocity is sent as 0; aftertouch, channel pressure and
//     system exclusive messages are not sent;
//   - stop, pause, the end of the song and each song start begin with
//     "all notes off" (controller 123) and "all sound off" (120) on all 16
//     channels; a song start (and a loop back to its beginning) then resets
//     controllers (121), sets the default pan of each channel, sends the
//     changed volumes/bends, and program 0 plus controller 7 to every
//     channel but the drums (10th);
//   - a seek (and a loop back to a loop start) plays the skipped events
//     silently, then sends "all notes off" and, per channel that changed,
//     program + controller 7, pan, pitch bend.
//
// The messages are queued under g_lock and sent to the backend by a thread
// of their own: a synthesizer call can take a while (a reset is ~110
// messages), and the game and timer threads must not wait for it, or the
// player's timing (midi_pos right after play_midi(), ...) would change.

// The MIDI output thread.
struct MidiPipe
{
   std::mutex m;
   std::condition_variable cv;
   std::vector<unsigned> q;
   bool quit;
   bool running;
   std::thread th;
   ~MidiPipe()
   {
      // remove_sound() was not called (no allegro_exit()): end the thread
      // rather than std::terminate() on a joinable std::thread.
      if (th.joinable()) {
         {
            std::lock_guard<std::mutex> lock(m);
            quit = true;
         }
         cv.notify_one();
         th.join();
      }
   }
};

MidiPipe g_pipe;

void pipe_main(const ZCPORT_MIDI_BACKEND *be)
{
   std::vector<unsigned> batch;
   std::unique_lock<std::mutex> lock(g_pipe.m);
   for (;;) {
      g_pipe.cv.wait(lock, [] { return g_pipe.quit || !g_pipe.q.empty(); });
      if (g_pipe.q.empty())
         break;   // quit, everything sent
      batch.swap(g_pipe.q);
      lock.unlock();
      for (size_t i = 0; i < batch.size(); i++)
         be->short_msg(batch[i]);
      batch.clear();
      lock.lock();
   }
}

void pipe_start(const ZCPORT_MIDI_BACKEND *be)
{
   g_pipe.quit = false;
   g_pipe.q.clear();
   g_pipe.th = std::thread(pipe_main, be);
   g_pipe.running = true;
}

// Sends what is queued, then ends the thread.
void pipe_stop()
{
   if (!g_pipe.running)
      return;
   {
      std::lock_guard<std::mutex> lock(g_pipe.m);
      g_pipe.quit = true;
   }
   g_pipe.cv.notify_one();
   g_pipe.th.join();
   g_pipe.running = false;
}

void pipe_send(unsigned msg)
{
   bool wake;
   {
      std::lock_guard<std::mutex> lock(g_pipe.m);
      wake = g_pipe.q.empty();
      g_pipe.q.push_back(msg);
   }
   if (wake)
      g_pipe.cv.notify_one();
}

const int kDefaultPan[16] =
{
   0x44, 0x68, 0x18, 0x3C, 0x68, 0x18, 0x44, 0x68,
   0x18, 0x3C, 0x68, 0x18, 0x44, 0x68, 0x18, 0x3C
};
const int kBendCentre = 0x2000;
const int kMaxLoopsPerStep = 64;   // a loop shorter than a beat floods no synth

struct MidiChan
{
   int prog, vol, new_vol, pan, bend, new_bend;
};

struct MidiState
{
   int prog, vol, new_vol, pan, bend, new_bend;
};

struct MidiOut
{
   const ZCPORT_MIDI_BACKEND *be;   // open backend, or NULL
   MidiChan ch[16];
   int sent_volume;                 // g_midi_volume at the last update
   MidiState skip[16];              // state while events are skipped
   uint32_t skip_tick;              // tick of the last event skipped
   bool skip_any;
   int loops;                       // loop backs in this player step
};

MidiOut g_mo;

void mo_init()
{
   for (int c = 0; c < 16; c++) {
      MidiChan &h = g_mo.ch[c];
      h.prog = 0;
      h.vol = h.new_vol = 127;
      h.pan = kDefaultPan[c];
      h.bend = h.new_bend = kBendCentre;
   }
   g_mo.sent_volume = -1;
   g_mo.loops = 0;
}

int mo_scale(int v)
{
   int mv = g_midi_volume < 0 ? 256 : g_midi_volume;
   return (v * mv) >> 8;
}

void mo_send(int status, int d1, int d2)
{
   if (!g_mo.be || g_mo.loops > kMaxLoopsPerStep)
      return;
   pipe_send((unsigned)(status & 0xFF) | ((unsigned)(d1 & 0x7F) << 8) |
             ((unsigned)(d2 & 0x7F) << 16));
}

void mo_send_bend(int c, int bend)
{
   mo_send(0xE0 | c, bend & 0x7F, (bend >> 7) & 0x7F);
}

void mo_all_notes_off()
{
   for (int c = 0; c < 16; c++) {
      mo_send(0xB0 | c, 123, 0);
      mo_send(0xB0 | c, 120, 0);
   }
}

void mo_update()
{
   bool vchg = g_midi_volume != g_mo.sent_volume;
   for (int c = 0; c < 16; c++) {
      MidiChan &h = g_mo.ch[c];
      if (h.new_vol != h.vol || vchg) {
         h.vol = h.new_vol;
         mo_send(0xB0 | c, 7, mo_scale(h.vol));
      }
      if (h.new_bend != h.bend) {
         h.bend = h.new_bend;
         mo_send_bend(c, h.bend);
      }
   }
   g_mo.sent_volume = g_midi_volume;
}

void mo_reset()
{
   mo_all_notes_off();
   for (int c = 0; c < 16; c++) {
      mo_send(0xB0 | c, 121, 0);
      mo_send(0xB0 | c, 10, kDefaultPan[c]);
      g_mo.ch[c].pan = kDefaultPan[c];
   }
   for (int c = 0; c < 16; c++) {
      MidiChan &h = g_mo.ch[c];
      h.new_vol = 127;
      h.new_bend = kBendCentre;
      h.prog = 0;
   }
   mo_update();
   for (int c = 0; c < 16; c++) {
      if (c == 9)
         continue;
      mo_send(0xC0 | c, 0, 0);
      mo_send(0xB0 | c, 7, mo_scale(g_mo.ch[c].vol));
   }
}

// An event the player plays.
void mo_event(const MidiEvent &e)
{
   if (!e.status)
      return;
   int c = e.status & 0x0F;
   MidiChan &h = g_mo.ch[c];
   switch (e.status & 0xF0) {
      case 0x80:
         mo_send(0x80 | c, e.d1, 0);
         break;
      case 0x90:
         mo_send(0x90 | c, e.d1, e.d2);
         break;
      case 0xB0:
         if (e.d1 == 7)
            h.new_vol = e.d2;
         else {
            if (e.d1 == 10)
               h.pan = e.d2;
            mo_send(0xB0 | c, e.d1, e.d2);
         }
         break;
      case 0xC0:
         h.prog = e.d1;
         mo_send(0xC0 | c, e.d1, 0);
         mo_send(0xB0 | c, 7, mo_scale(h.vol));
         break;
      case 0xE0:
         h.new_bend = e.d1 | (e.d2 << 7);
         break;
      default:   // aftertouch, channel pressure: not sent
         break;
   }
}

// Skipping events (seek, loop back to a loop start): from the start of the
// song (defaults) or from where the player is.
void mo_skip_begin(bool from_start)
{
   for (int c = 0; c < 16; c++) {
      MidiState &s = g_mo.skip[c];
      const MidiChan &h = g_mo.ch[c];
      if (from_start) {
         s.prog = 0;
         s.vol = s.new_vol = 127;
         s.pan = kDefaultPan[c];
         s.bend = s.new_bend = kBendCentre;
      }
      else {
         s.prog = h.prog;
         s.vol = h.vol;
         s.new_vol = h.new_vol;
         s.pan = h.pan;
         s.bend = h.bend;
         s.new_bend = h.new_bend;
      }
   }
   g_mo.skip_any = false;
}

// The update of a player step, silently: pending volumes and bends apply.
void mo_skip_update()
{
   for (int c = 0; c < 16; c++) {
      MidiState &s = g_mo.skip[c];
      s.vol = s.new_vol;
      s.bend = s.new_bend;
   }
}

// As the reference, the skipped events are played silently tick by tick:
// controller 7 and pitch bend apply at the next tick (so those of the very
// last tick of a song stay pending when a seek runs into its end).
void mo_skip_event(const MidiEvent &e)
{
   if (!g_mo.skip_any || e.tick != g_mo.skip_tick) {
      mo_skip_update();
      g_mo.skip_tick = e.tick;
      g_mo.skip_any = true;
   }
   if (!e.status)
      return;
   MidiState &s = g_mo.skip[e.status & 0x0F];
   switch (e.status & 0xF0) {
      case 0xB0:
         if (e.d1 == 7)
            s.new_vol = e.d2;
         else if (e.d1 == 10)
            s.pan = e.d2;
         break;
      case 0xC0:
         s.prog = e.d1;
         break;
      case 0xE0:
         s.new_bend = e.d1 | (e.d2 << 7);
         break;
   }
}

// Takes the skipped state over. 'reached' (the seek found its target):
// the last tick's update, "all notes off", then what changed is sent.
void mo_skip_end(bool reached)
{
   if (reached) {
      mo_skip_update();
      mo_all_notes_off();
   }
   for (int c = 0; c < 16; c++) {
      const MidiState &s = g_mo.skip[c];
      MidiChan &h = g_mo.ch[c];
      if (reached) {
         if (s.prog != h.prog || s.vol != h.vol) {
            mo_send(0xC0 | c, s.prog, 0);
            mo_send(0xB0 | c, 7, mo_scale(s.vol));
         }
         if (s.pan != h.pan)
            mo_send(0xB0 | c, 10, s.pan);
         if (s.bend != h.bend)
            mo_send_bend(c, s.bend);
      }
      h.prog = s.prog;
      h.vol = s.vol;
      h.new_vol = s.new_vol;
      h.pan = s.pan;
      h.bend = s.bend;
      h.new_bend = s.new_bend;
   }
}

// ---------------------------------------------------------------------------
// MIDI player (continued)
// ---------------------------------------------------------------------------

double ticks_to_us(uint32_t ticks, int tempo)
{
   return (double)ticks * tempo / g_mp.divisions;
}

long beat_of(uint32_t tick)
{
   return (long)(tick / (uint32_t)g_mp.divisions) + 1;
}

void set_time_from_us()
{
   double us = g_mp.song_us;
   midi_time = us > 0 ? (long)((us - 1) / 1000000.0) : 0;
}

void rewind_song()
{
   g_mp.cursor = 0;
   g_mp.tick = 0;
   g_mp.song_us = 0;
   g_mp.tempo = 500000;
}

// Moves to the event 'idx' without playing it (time and tempo follow).
void advance_to(size_t idx)
{
   const MidiEvent &e = g_mp.events[idx];
   if (e.tick > g_mp.tick) {
      g_mp.song_us += ticks_to_us(e.tick - g_mp.tick, g_mp.tempo);
      g_mp.tick = e.tick;
   }
}

// Plays (silently) events while their beat is below 'target'. Returns false
// at the end of the file.
bool skip_until(long target)
{
   while (g_mp.cursor < g_mp.events.size()) {
      const MidiEvent &e = g_mp.events[g_mp.cursor];
      if (beat_of(e.tick) >= target) {
         advance_to(g_mp.cursor);
         return true;
      }
      advance_to(g_mp.cursor);
      if (e.tempo > 0)
         g_mp.tempo = e.tempo;
      midi_pos = beat_of(e.tick);
      set_time_from_us();          // the reference's midi_time follows too
      mo_skip_event(e);
      g_mp.cursor++;
   }
   return false;
}

void end_song()
{
   g_mp.playing = false;
   midi_pos = -((long)(g_mp.end_tick / (uint32_t)g_mp.divisions) + 1);
   mo_all_notes_off();
}

void loop_back()
{
   g_mo.loops++;
   rewind_song();
   if (midi_loop_start > 0) {
      mo_skip_begin(true);
      if (!skip_until(midi_loop_start)) {
         rewind_song();
         mo_skip_end(false);
         mo_reset();
      }
      else
         mo_skip_end(true);
      midi_pos = midi_loop_start;
   }
   else
      mo_reset();
}

// One player step at wall time 'now': plays the events that are due.
void player_step(uint64_t now)
{
   if (!g_mp.playing || g_mp.paused || now < g_mp.next_fire)
      return;
   g_mo.loops = 0;
   uint64_t fire = g_mp.next_fire;
   for (int guard = 0; guard < 100000; guard++) {
      if (g_mp.cursor >= g_mp.events.size()) {
         if (g_mp.looping) {
            loop_back();
            if (g_mp.events.empty()) {
               end_song();
               return;
            }
            continue;
         }
         end_song();
         return;
      }
      const MidiEvent &e = g_mp.events[g_mp.cursor];
      if (e.tick > g_mp.tick) {
         // Wait for this event.
         double wait = ticks_to_us(e.tick - g_mp.tick, g_mp.tempo);
         g_mp.last_wait = (uint64_t)wait;
         g_mp.period = g_mp.last_wait;
         g_mp.song_us += wait;
         g_mp.tick = e.tick;
         g_mp.next_fire = fire + (uint64_t)wait;
         if (g_mp.next_fire > now) {
            mo_update();
            g_mo.loops = 0;
            return;
         }
         fire = g_mp.next_fire;
         continue;
      }
      if (e.tempo > 0)
         g_mp.tempo = e.tempo;
      midi_pos = beat_of(e.tick);
      set_time_from_us();
      mo_event(e);
      g_mp.cursor++;
      if (g_mp.looping && midi_loop_end > 0 && midi_pos >= midi_loop_end) {
         loop_back();
      }
   }
   mo_update();
   g_mo.loops = 0;
}

void midi_timer(void)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   player_step(now_us());
}

void stop_player()
{
   if (g_mp.playing) {
      // The reference negates the beat number (-1 before the first event).
      g_mp.playing = false;
      midi_pos = (midi_pos > 0) ? -midi_pos : -1;
   }
   g_mp.midi = NULL;
}

// install_sound(): opens the registered MIDI backend (g_lock held).
void open_midi_output()
{
   mo_init();
   g_mo.be = NULL;
   const ZCPORT_MIDI_BACKEND *be = g_midi_backend;
   set_name(midi_zcport_drv, kMidiSilentName);
   if (be && be->short_msg && be->open && be->open() == 0) {
      g_mo.be = be;
      pipe_start(be);
      if (be->name)
         set_name(midi_zcport_drv, be->name);
      if (be->render)
         g_mix_synth = be;
   }
}

// remove_sound(), after the digital output is closed (render() is no longer
// called). What is queued is sent first.
void close_midi_output()
{
   const ZCPORT_MIDI_BACKEND *be = g_mo.be;
   g_mo.be = NULL;
   g_mix_synth = NULL;
   pipe_stop();
   if (be) {
      if (be->reset)
         be->reset();
      if (be->close)
         be->close();
   }
   set_name(midi_zcport_drv, kMidiSilentName);
}

// ---------------------------------------------------------------------------
// audio streams
// ---------------------------------------------------------------------------

// As in the reference: the voice loops over a ring of 2 x bufcount buffers
// of silence. At the start every buffer is handed out at once (in order);
// after that, a whole half is handed out (buffer after buffer) once the
// voice has moved on into the other half.
//
// The mixer, a little behind the game's clock, would still be playing the
// end of a half the game refills at once: it plays its own copy of the
// ring, and each buffer the game gives back reaches that copy at the mixer
// time it was handed out (for the first round, the start of the stream).
struct Stream
{
   AUDIOSTREAM st;
   SAMPLE spl;
   std::vector<unsigned char> data;      // the game's buffers (st.samp->data)
   std::vector<unsigned char> mixdata;   // what the mixer plays
   int len;
   int bufcount;
   int bytes_per_buffer;
   int next_buf;          // the next buffer to hand out
   int locked_buf;
   int active;            // the half the voice is taken to be in
   long given;            // buffers handed out so far
   uint64_t t_start;
   uint64_t t_given;      // mixer time of the locked buffer's data
   bool locked;
   Stream *next;
};

Stream *g_streams = NULL;

// The stream voice's position as voice_get_position() reads it.
int stream_voice_pos(int v)
{
   if (!valid(v) || g_voice[v].phys < 0)
      return -1;
   Voice &vc = g_voice[v];
   update(vc);
   return vc.playing ? (int)vc.pos : -1;
}

void sound_exit_func(void)
{
   remove_sound();
}

} // namespace

// ---------------------------------------------------------------------------
// installation and volume
// ---------------------------------------------------------------------------

extern "C" int detect_digi_driver(int driver_id)
{
   return (driver_id == DIGI_NONE) ? 0 : digi_zcport_drv.voices;
}

extern "C" int detect_midi_driver(int driver_id)
{
   return (driver_id == MIDI_NONE) ? 0 : 16;
}

extern "C" void reserve_voices(int digi_voices, int midi_voices)
{
   (void)digi_voices;
   (void)midi_voices;
}

extern "C" void set_volume_per_voice(int scale)
{
   g_mix_scale = scale;
}

extern "C" int install_sound(int digi, int midi, const char *cfg_path)
{
   (void)cfg_path;
   {
      std::lock_guard<std::recursive_mutex> guard(g_lock);
      if (g_installed)
         return 0;
   }
   if (!timer_driver)
      install_timer();
   // The MIDI player's timer (a slot of its own, as in the reference).
   // Installed outside the sound lock: the timer thread calls midi_timer()
   // with the timer lock held, which then takes the sound lock.
   bool timer_on = install_int_ex(midi_timer, MSEC_TO_TIMER(1)) == 0;

   std::lock_guard<std::recursive_mutex> guard(g_lock);
   g_mp.timer_on = timer_on;
   init_voices();
   for (int i = 0; i < kVoices; i++)
      g_voice[i].used = false;
   for (int i = 0; i < kPhysical; i++)
      g_phys_owner[i] = -1;

   digi_card = digi;
   midi_card = midi;
   digi_driver = (digi == DIGI_NONE) ? &digi_none_drv : &digi_zcport_drv;
   midi_driver = (midi == MIDI_NONE) ? &midi_none_drv : &midi_zcport_drv;

   // The outputs. Without a device the sound is silent, nothing else changes.
   set_name(digi_zcport_drv, kDigiSilentName);
   if (digi != DIGI_NONE && open_output())
      set_name(digi_zcport_drv, kDigiName);
   mo_init();
   g_mo.be = NULL;
   if (midi != MIDI_NONE)
      open_midi_output();

   g_installed = true;
   _add_exit_func(sound_exit_func, "remove_sound");
   return 0;
}

extern "C" void remove_sound(void)
{
   bool timer_on;
   {
      std::lock_guard<std::recursive_mutex> guard(g_lock);
      if (!g_installed)
         return;
      timer_on = g_mp.timer_on;
      g_mp.timer_on = false;
   }
   if (timer_on)
      remove_int(midi_timer);      // outside the sound lock (see install_sound)

   std::lock_guard<std::recursive_mutex> guard(g_lock);
   stop_player();
   mo_all_notes_off();
   while (g_streams)
      stop_audio_stream(&g_streams->st);
   for (int i = 0; i < kVoices; i++)
      if (g_voice[i].used)
         free_voice(i);
   close_output();
   close_midi_output();
   set_name(digi_zcport_drv, kDigiSilentName);
   digi_driver = &digi_none_drv;
   midi_driver = &midi_none_drv;
   g_installed = false;
   _remove_exit_func(sound_exit_func);
}

extern "C" int install_sound_input(int digi, int midi)
{
   (void)digi;
   (void)midi;
   return -1;
}

extern "C" void remove_sound_input(void)
{
}

extern "C" void set_volume(int digi_volume, int midi_volume)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (digi_volume >= 0) {
      g_digi_volume = digi_volume > 255 ? 255 : digi_volume;
      g_mix_digi = g_digi_volume;
   }
   if (midi_volume >= 0)
      g_midi_volume = midi_volume > 255 ? 255 : midi_volume;
}

extern "C" void get_volume(int *digi_volume, int *midi_volume)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (digi_volume)
      *digi_volume = g_digi_volume;
   if (midi_volume)
      *midi_volume = g_midi_volume;
}

extern "C" void set_hardware_volume(int digi_volume, int midi_volume)
{
   set_volume(digi_volume, midi_volume);
}

extern "C" void get_hardware_volume(int *digi_volume, int *midi_volume)
{
   get_volume(digi_volume, midi_volume);
}

static int g_mixer_quality = 2;

extern "C" void set_mixer_quality(int quality)
{
   g_mixer_quality = quality;
   g_mix_quality = quality;
}

extern "C" int get_mixer_quality(void) { return g_mixer_quality; }
extern "C" int get_mixer_frequency(void) { return 44100; }
extern "C" int get_mixer_bits(void) { return 16; }
extern "C" int get_mixer_channels(void) { return 2; }
extern "C" int get_mixer_voices(void) { return kPhysical; }
extern "C" int get_mixer_buffer_length(void) { return 256; }

// ---------------------------------------------------------------------------
// voices
// ---------------------------------------------------------------------------

extern "C" int allocate_voice(const SAMPLE *spl)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   return alloc_voice(spl);
}

extern "C" void deallocate_voice(int voice)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (valid(voice))
      free_voice(voice);
}

extern "C" void reallocate_voice(int voice, const SAMPLE *spl)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (!valid(voice) || !spl)
      return;
   release_phys(g_voice[voice]);
   setup_voice(voice, spl);
}

extern "C" void release_voice(int voice)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (valid(voice))
      g_voice[voice].autokill = true;
}

extern "C" void voice_start(int voice)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (!valid(voice) || g_voice[voice].phys < 0)
      return;
   Voice &vc = g_voice[voice];
   update(vc);
   if (!vc.playing) {
      vc.playing = true;
      vc.ended = false;
      vc.t0 = now_us();
      publish(voice);
   }
}

extern "C" void voice_stop(int voice)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (!valid(voice))
      return;
   Voice &vc = g_voice[voice];
   update(vc);
   vc.playing = false;
   publish(voice);
}

extern "C" void voice_set_priority(int voice, int priority)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (valid(voice))
      g_voice[voice].priority = priority;
}

extern "C" SAMPLE *voice_check(int voice)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (!valid(voice))
      return NULL;
   Voice &vc = g_voice[voice];
   if (vc.phys < 0)
      return NULL;
   update(vc);
   if (vc.autokill && !vc.playing)
      return NULL;
   return (SAMPLE *)vc.spl;
}

extern "C" void voice_set_playmode(int voice, int playmode)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (!valid(voice))
      return;
   Voice &vc = g_voice[voice];
   update(vc);
   vc.playmode = playmode;
   publish(voice);
}

extern "C" int voice_get_position(int voice)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (!valid(voice) || g_voice[voice].phys < 0)
      return -1;
   Voice &vc = g_voice[voice];
   update(vc);
   if (!vc.playing)
      return -1;
   return (int)vc.pos;
}

extern "C" void voice_set_position(int voice, int position)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (!valid(voice) || g_voice[voice].phys < 0)
      return;
   Voice &vc = g_voice[voice];
   update(vc);
   vc.pos = position < 0 ? 0 : position;
   vc.t0 = now_us();
   vc.ended = false;
   publish(voice, true);
}

extern "C" int voice_get_volume(int voice)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (!valid(voice) || g_voice[voice].phys < 0)
      return -1;
   return g_voice[voice].vol;
}

extern "C" void voice_set_volume(int voice, int volume)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (valid(voice) && g_voice[voice].phys >= 0) {
      g_voice[voice].vol = volume;
      publish(voice);
   }
}

extern "C" void voice_ramp_volume(int voice, int tyme, int endvol)
{
   (void)tyme;
   voice_set_volume(voice, endvol);
}

extern "C" void voice_stop_volumeramp(int voice)
{
   (void)voice;
}

extern "C" int voice_get_frequency(int voice)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (!valid(voice) || g_voice[voice].phys < 0)
      return -1;
   return g_voice[voice].freq;
}

extern "C" void voice_set_frequency(int voice, int frequency)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (!valid(voice) || g_voice[voice].phys < 0)
      return;
   Voice &vc = g_voice[voice];
   update(vc);
   vc.freq = frequency > 0 ? frequency : 1;
   publish(voice);
}

extern "C" void voice_sweep_frequency(int voice, int tyme, int endfreq)
{
   (void)tyme;
   voice_set_frequency(voice, endfreq);
}

extern "C" void voice_stop_frequency_sweep(int voice)
{
   (void)voice;
}

extern "C" int voice_get_pan(int voice)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (!valid(voice) || g_voice[voice].phys < 0)
      return -1;
   return g_voice[voice].pan;
}

extern "C" void voice_set_pan(int voice, int pan)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (valid(voice) && g_voice[voice].phys >= 0) {
      g_voice[voice].pan = pan;
      publish(voice);
   }
}

extern "C" void voice_sweep_pan(int voice, int tyme, int endpan)
{
   (void)tyme;
   voice_set_pan(voice, endpan);
}

extern "C" void voice_stop_pan_sweep(int voice)
{
   (void)voice;
}

extern "C" void voice_set_echo(int voice, int strength, int delay)
{
   (void)voice; (void)strength; (void)delay;
}

extern "C" void voice_set_tremolo(int voice, int rate, int depth)
{
   (void)voice; (void)rate; (void)depth;
}

extern "C" void voice_set_vibrato(int voice, int rate, int depth)
{
   (void)voice; (void)rate; (void)depth;
}

// ---------------------------------------------------------------------------
// samples
// ---------------------------------------------------------------------------

extern "C" int play_sample(const SAMPLE *spl, int vol, int pan, int freq, int loop)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   int v = alloc_voice(spl);
   if (v < 0)
      return -1;
   Voice &vc = g_voice[v];
   vc.autokill = true;
   if (vc.phys >= 0) {
      vc.vol = vol;
      vc.pan = pan;
      long f = (long)((long long)spl->freq * freq / 1000);
      vc.freq = f > 0 ? (int)f : 1;
      vc.playmode = loop ? PLAYMODE_LOOP : PLAYMODE_PLAY;
      vc.playing = true;
      vc.t0 = now_us();
      publish(v);
   }
   return v;
}

extern "C" void stop_sample(const SAMPLE *spl)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   for (int i = 0; i < kVoices; i++)
      if (g_voice[i].used && g_voice[i].spl == spl)
         free_voice(i);
}

extern "C" void adjust_sample(const SAMPLE *spl, int vol, int pan, int freq, int loop)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   for (int i = 0; i < kVoices; i++) {
      Voice &vc = g_voice[i];
      if (!vc.used || vc.spl != spl)
         continue;
      // The first voice with this sample, as in the reference (also one
      // that has already ended).
      if (vc.phys >= 0) {
         update(vc);
         vc.vol = vol;
         vc.pan = pan;
         long f = (long)((long long)spl->freq * freq / 1000);
         vc.freq = f > 0 ? (int)f : 1;
         vc.playmode = loop ? (vc.playmode | PLAYMODE_LOOP) : (vc.playmode & ~PLAYMODE_LOOP);
         publish(i);
      }
      return;
   }
}

extern "C" void zcport_sound_forget_sample(SAMPLE *spl)
{
   if (!spl)
      return;
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   stop_sample(spl);
   mix_forget(spl);
}

// ---------------------------------------------------------------------------
// MIDI
// ---------------------------------------------------------------------------

extern "C" int play_midi(MIDI *midi, int loop)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   stop_player();
   if (!midi) {
      mo_all_notes_off();
      return 0;
   }
   if (!g_installed)
      return -1;
   load_events(midi);
   g_mp.midi = midi;
   rewind_song();
   g_mp.looping = loop != 0;
   g_mp.paused = false;
   g_mp.playing = true;
   g_mp.next_fire = now_us();
   // Until the first wait between events, the player's timer period is one
   // beat (what midi_resume() restarts it with).
   g_mp.last_wait = (uint64_t)ticks_to_us((uint32_t)g_mp.divisions, g_mp.tempo);
   g_mp.period = g_mp.last_wait;
   midi_pos = 0;
   midi_time = 0;
   midi_loop_start = -1;
   midi_loop_end = -1;
   mo_reset();
   return 0;
}

extern "C" int play_looped_midi(MIDI *midi, int loop_start, int loop_end)
{
   int r = play_midi(midi, TRUE);
   if (r == 0) {
      std::lock_guard<std::recursive_mutex> guard(g_lock);
      midi_loop_start = loop_start;
      midi_loop_end = loop_end;
   }
   return r;
}

extern "C" void stop_midi(void)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   stop_player();
   mo_all_notes_off();
}

extern "C" void midi_pause(void)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (g_mp.playing && !g_mp.paused) {
      g_mp.paused = true;
      mo_all_notes_off();
   }
}

extern "C" void midi_resume(void)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (g_mp.playing && g_mp.paused) {
      g_mp.paused = false;
      // The player's timer restarts with its last period.
      g_mp.next_fire = now_us() + g_mp.last_wait;
      g_mp.period = g_mp.last_wait;
   }
   else if (g_mp.playing && g_mp.period != g_mp.last_wait) {
      // A running timer (a seek restarted it) is set to the last period
      // again, as the reference does: its next call moves by the difference.
      g_mp.next_fire = g_mp.next_fire - g_mp.period + g_mp.last_wait;
      g_mp.period = g_mp.last_wait;
   }
}

extern "C" int midi_seek(int target)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (!g_mp.playing)
      return -1;
   // As the reference: a target at or before the current beat starts again
   // from the beginning (midi_pos 0, then the beats skipped); midi_time
   // follows the skipped events.
   bool from_start = target <= midi_pos;
   if (from_start) {
      rewind_song();
      midi_pos = 0;
      midi_time = 0;
   }
   mo_skip_begin(from_start);
   if (skip_until(target)) {
      mo_skip_end(true);
      // The player goes on from the event reached at its next timer tick,
      // whatever its schedule was, and a paused player runs again (the
      // reference: the game pauses again itself after a seek).
      g_mp.paused = false;
      g_mp.next_fire = now_us() + 1000;
      g_mp.period = 0;
      return 0;
   }
   mo_skip_end(false);
   if (g_mp.looping) {
      // Past the end of a looping song: from the start, at once.
      rewind_song();
      midi_pos = 0;
      midi_time = 0;
      g_mp.paused = false;
      g_mp.next_fire = now_us() + 1000;
      g_mp.period = 0;
      mo_reset();
      return 2;
   }
   end_song();
   return 1;
}

extern "C" void midi_out(unsigned char *data, int length)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (!g_installed || !data)
      return;
   // The messages go through the player's handling (controller 7 scaled,
   // ...), then the channels are updated, as the reference does.
   unsigned char status = 0;
   int i = 0;
   while (i < length) {
      unsigned char c = data[i];
      if (c >= 0xF0) {
         // system messages are not sent; skip a system exclusive block
         i++;
         if (c == 0xF0)
            while (i < length && data[i++] != 0xF7) {}
         continue;
      }
      if (c & 0x80) {
         status = c;
         i++;
      }
      if (!status)
         break;
      int hi = status & 0xF0;
      int bytes = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
      MidiEvent e;
      e.tick = 0;
      e.tempo = -1;
      e.status = status;
      e.d1 = (unsigned char)(i < length ? data[i] & 0x7F : 0);
      e.d2 = (unsigned char)(bytes == 2 && i + 1 < length ? data[i + 1] & 0x7F : 0);
      i += bytes;
      mo_event(e);
   }
   mo_update();
}

extern "C" int load_midi_patches(void)
{
   return 0;
}

extern "C" void zcport_sound_forget_midi(MIDI *midi)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (midi && g_mp.midi == midi) {
      stop_player();
      mo_all_notes_off();
   }
}

extern "C" void zcport_set_midi_backend(const ZCPORT_MIDI_BACKEND *backend)
{
   g_midi_backend = backend;
}

extern "C" const ZCPORT_MIDI_BACKEND *zcport_get_midi_backend(void)
{
   return g_midi_backend;
}

// ---------------------------------------------------------------------------
// audio streams
// ---------------------------------------------------------------------------

extern "C" AUDIOSTREAM *play_audio_stream(int len, int bits, int stereo, int freq, int vol, int pan)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   if (!g_installed || len <= 0 || freq <= 0 || (bits != 8 && bits != 16))
      return NULL;
   Stream *s = new Stream;
   memset(&s->st, 0, sizeof(s->st));
   memset(&s->spl, 0, sizeof(s->spl));
   s->len = len;
   s->bufcount = (6144 + len - 1) / len;
   if (s->bufcount < 1)
      s->bufcount = 1;
   int frame = (bits / 8) * (stereo ? 2 : 1);
   s->bytes_per_buffer = len * frame;
   // Silence in Allegro's unsigned sample format (0x80 / 0x8000).
   size_t bytes = (size_t)s->bytes_per_buffer * s->bufcount * 2;
   if (bits == 8)
      s->data.assign(bytes, 0x80);
   else {
      s->data.assign(bytes, 0);
      for (size_t b = 0; b + 1 < bytes; b += 2) {
         unsigned short half = 0x8000;
         memcpy(&s->data[b], &half, 2);
      }
   }
   s->mixdata = s->data;
   s->spl.bits = bits;
   s->spl.stereo = stereo;
   s->spl.freq = freq;
   s->spl.priority = 255;
   s->spl.len = (unsigned long)(len * s->bufcount * 2);
   s->spl.loop_start = 0;
   s->spl.loop_end = s->spl.len;
   s->spl.data = &s->data[0];
   int v = alloc_voice(&s->spl);
   if (v < 0) {
      delete s;
      return NULL;
   }
   g_voice[v].data = &s->mixdata[0];   // the mixer plays its copy
   voice_set_playmode(v, PLAYMODE_LOOP);
   voice_set_volume(v, vol);
   voice_set_pan(v, pan);
   voice_start(v);
   s->st.voice = v;
   s->st.samp = &s->spl;
   s->st.len = len;
   s->st.bufcount = s->bufcount;
   s->st.bufnum = 0;
   s->st.active = 1;
   s->st.locked = NULL;
   s->next_buf = 0;
   s->locked_buf = 0;
   s->active = 1;
   s->given = 0;
   s->t_start = g_voice[v].t0;
   s->t_given = s->t_start;
   s->locked = false;
   s->next = g_streams;
   g_streams = s;
   return &s->st;
}

static Stream *find_stream(AUDIOSTREAM *stream)
{
   for (Stream *s = g_streams; s; s = s->next)
      if (&s->st == stream)
         return s;
   return NULL;
}

extern "C" void stop_audio_stream(AUDIOSTREAM *stream)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   Stream **pp = &g_streams;
   while (*pp) {
      if (&(*pp)->st == stream) {
         Stream *s = *pp;
         *pp = s->next;
         if (valid(s->st.voice))
            free_voice(s->st.voice);
         mix_forget(&s->spl);
         delete s;
         return;
      }
      pp = &(*pp)->next;
   }
}

extern "C" void *get_audio_stream_buffer(AUDIOSTREAM *stream)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   Stream *s = find_stream(stream);
   if (!s)
      return NULL;
   if (s->locked)
      return stream->locked;
   const int half = s->next_buf / s->bufcount;
   if (s->next_buf % s->bufcount == 0 && half == s->active) {
      // The voice was in this half: wait until it has moved into the other
      // one (a stopped voice reads -1, i.e. the first half).
      bool in_second = stream_voice_pos(s->st.voice) >= s->len * s->bufcount;
      if (half == 0 ? !in_second : in_second)
         return NULL;
      s->active = 1 - half;
   }
   const int idx = s->next_buf;
   s->next_buf = (idx + 1) % (2 * s->bufcount);
   // The first round fills the ring before the voice gets to any of it.
   s->t_given = s->given < 2L * s->bufcount ? s->t_start : now_us();
   s->given++;
   s->locked = true;
   s->locked_buf = idx;
   stream->bufnum = idx;
   stream->active = s->active;
   stream->locked = &s->data[(size_t)idx * s->bytes_per_buffer];
   return stream->locked;
}

extern "C" void free_audio_stream_buffer(AUDIOSTREAM *stream)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   Stream *s = find_stream(stream);
   if (!s)
      return;
   if (s->locked) {
      const size_t at = (size_t)s->locked_buf * s->bytes_per_buffer;
      publish_data(&s->spl, s->t_given, &s->mixdata[at], &s->data[at], (size_t)s->bytes_per_buffer);
   }
   s->locked = false;
   stream->locked = NULL;
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

extern "C" int zcport_sound_output_active(void)
{
   std::lock_guard<std::recursive_mutex> guard(g_lock);
   return g_mix.open ? 1 : 0;
}

extern "C" void zcport_sound_set_tap(ZCPORT_SOUND_TAP tap, void *user)
{
   g_tap_user = user;
   g_tap = tap;
}

// ---------------------------------------------------------------------------
// sound input (not supported, like the reference without a recording driver)
// ---------------------------------------------------------------------------

extern "C" int get_sound_input_cap_bits(void) { return 0; }
extern "C" int get_sound_input_cap_stereo(void) { return 0; }
extern "C" int get_sound_input_cap_rate(int bits, int stereo) { (void)bits; (void)stereo; return 0; }
extern "C" int get_sound_input_cap_parm(int rate, int bits, int stereo) { (void)rate; (void)bits; (void)stereo; return 0; }
extern "C" int set_sound_input_source(int source) { (void)source; return -1; }
extern "C" int start_sound_input(int rate, int bits, int stereo) { (void)rate; (void)bits; (void)stereo; return 0; }
extern "C" void stop_sound_input(void) {}
extern "C" int read_sound_input(void *buffer) { (void)buffer; return 0; }

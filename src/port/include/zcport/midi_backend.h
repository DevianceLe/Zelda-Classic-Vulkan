/*
 * zcport internal contract: where the MIDI player's output goes.
 *
 * The MIDI player (src/port/al/platform_sound.cpp) turns the song into the
 * same stream of channel messages the reference build hands its Windows MIDI
 * driver (resets, controller 7 scaled by the MIDI volume, ...) and sends
 * them to the registered backend. Without a backend (or when open() fails)
 * the music is silent, but the player's timing and state are unchanged.
 *
 * Backends: winmm (MIDI mapper) on Windows, registered by the SDL platform
 * layer (src/port/platform/midi_winmm.cpp); a software synthesizer on
 * Android (midi_tsf.cpp, which implements render()); none elsewhere yet.
 *
 * Threads: open/close are called from install_sound()/remove_sound();
 * short_msg/long_msg/reset from the game thread or the timer thread, never at
 * the same time (the sound module serialises them); render() from the audio
 * thread, concurrently with the others.
 *
 * C++98 compatible header.
 */

#ifndef ZCPORT_MIDI_BACKEND_H
#define ZCPORT_MIDI_BACKEND_H

#ifdef __cplusplus
   extern "C" {
#endif

typedef struct ZCPORT_MIDI_BACKEND
{
   /* Shown as midi_driver->name while the backend is open. */
   const char *name;

   /* Start the synthesizer: 0 on success. */
   int  (*open)(void);
   void (*close)(void);

   /* One channel message: status | data1 << 8 | data2 << 16 (the layout of
    * midiOutShortMsg). */
   void (*short_msg)(unsigned int msg);

   /* A complete system exclusive message (F0 ... F7). May be NULL. */
   void (*long_msg)(const unsigned char *data, int length);

   /* Silence every channel at once (used before close). May be NULL. */
   void (*reset)(void);

   /* Software synthesizers only (NULL otherwise): add 'frames' stereo
    * frames at 'freq' Hz to 'mix' (interleaved left/right, 16-bit full scale
    * = 32768.0). Called by the digital mixer from the audio thread. */
   void (*render)(float *mix, int frames, int freq);
} ZCPORT_MIDI_BACKEND;

/* Registers the backend used by the next install_sound() (NULL: silent).
 * The structure must stay valid. Safe to call before main(). */
void zcport_set_midi_backend(const ZCPORT_MIDI_BACKEND *backend);
const ZCPORT_MIDI_BACKEND *zcport_get_midi_backend(void);

#ifdef __cplusplus
   }
#endif

#endif /* ZCPORT_MIDI_BACKEND_H */

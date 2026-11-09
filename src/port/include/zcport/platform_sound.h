/*
 * zcport internal contract: what the sound module (platform module) needs
 * from the code that frees samples and MIDI files (files module), plus two
 * hooks for tests.
 *
 * Voices, audio streams and the MIDI player keep their own state and timing
 * (voice positions advance in real time, midi_pos counts beats and goes
 * negative at the end), exactly as without a sound device, so game logic
 * that waits for sounds or music behaves as with the reference build. The
 * digital mixer and the MIDI output only follow that state.
 *
 * C++98 compatible header.
 */

#ifndef ZCPORT_PLATFORM_SOUND_H
#define ZCPORT_PLATFORM_SOUND_H

#ifdef __cplusplus
   extern "C" {
#endif

struct SAMPLE;
struct MIDI;

/* destroy_sample() must call this before freeing 'spl': stops and frees the
 * voices that play it (Allegro stops a sample that is destroyed) and makes
 * the mixer forget it at once. */
void zcport_sound_forget_sample(struct SAMPLE *spl);

/* destroy_midi() must call this before freeing 'midi': stops the MIDI player
 * if it plays that file. */
void zcport_sound_forget_midi(struct MIDI *midi);

/* ---- for tests --------------------------------------------------------------- */

/* 1 while install_sound() has an audio device open (digital sound is
 * audible), 0 otherwise. */
int zcport_sound_output_active(void);

/* Called from the audio thread with every block the mixer hands the device
 * (interleaved stereo, signed 16-bit), after mixing. NULL removes it. */
typedef void (*ZCPORT_SOUND_TAP)(void *user, const short *frames, int count);
void zcport_sound_set_tap(ZCPORT_SOUND_TAP tap, void *user);

#ifdef __cplusplus
   }
#endif

#endif /* ZCPORT_PLATFORM_SOUND_H */

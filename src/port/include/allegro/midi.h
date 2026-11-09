/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/midi.h - MIDI music.
 *
 * The game reads MIDI->divisions and MIDI->track[].data/len directly
 * (src/midi.cpp) and midi_driver->name (zc_sys.cpp, compares "DIGMID").
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_MIDI_H
#define ALLEGRO_MIDI_H

#include "base.h"

#ifdef __cplusplus
   extern "C" {
#endif

#define MIDI_VOICES           64       /* actual drivers may not be */
#define MIDI_TRACKS           32       /* able to handle this many */

typedef struct MIDI                    /* a midi file */
{
   int divisions;                      /* number of ticks per quarter note */
   struct {
      unsigned char *data;             /* MIDI message stream */
      int len;                         /* length of the track data */
   } track[MIDI_TRACKS];
} MIDI;

#define MIDI_AUTODETECT       -1
#define MIDI_NONE             0
#define MIDI_DIGMID           AL_ID('D','I','G','I')
#define MIDI_WIN32MAPPER      AL_ID('W','3','2','M')
#define MIDI_WIN32(n)         AL_ID('W','3','2','A'+(n))
#define MIDI_WIN32_IN(n)      AL_ID('W','3','2','A'+(n))
#define MIDI_ZCPORT           AL_ID('Z','C','P','T')

typedef struct MIDI_DRIVER             /* driver for playing midi music */
{
   int  id;
   AL_CONST char *name;
   AL_CONST char *desc;
   AL_CONST char *ascii_name;
   int  voices;
   int  basevoice;
   int  max_voices;
   int  def_voices;
   int  xmin, xmax;

   AL_METHOD(int,  detect, (int input));
   AL_METHOD(int,  init, (int input, int voices));
   AL_METHOD(void, exit, (int input));
   AL_METHOD(int,  set_mixer_volume, (int volume));
   AL_METHOD(int,  get_mixer_volume, (void));
   AL_METHOD(void, raw_midi, (int data));
   AL_METHOD(int,  load_patches, (AL_CONST char *patches, AL_CONST char *drums));
   AL_METHOD(void, adjust_patches, (AL_CONST char *patches, AL_CONST char *drums));
   AL_METHOD(void, key_on, (int inst, int note, int bend, int vol, int pan));
   AL_METHOD(void, key_off, (int voice));
   AL_METHOD(void, set_volume, (int voice, int vol));
   AL_METHOD(void, set_pitch, (int voice, int note, int bend));
   AL_METHOD(void, set_pan, (int voice, int pan));
   AL_METHOD(void, set_vibrato, (int voice, int amount));
} MIDI_DRIVER;

AL_VAR(MIDI_DRIVER, midi_digmid);

AL_VAR(MIDI_DRIVER *, midi_driver);
AL_VAR(MIDI_DRIVER *, midi_input_driver);
AL_VAR(int, midi_card);
AL_VAR(int, midi_input_card);

AL_VAR(volatile long, midi_pos);       /* current position in the midi file, in beats */
AL_VAR(volatile long, midi_time);      /* current position in the midi file, in seconds */

AL_VAR(long, midi_loop_start);         /* where to loop back to at EOF */
AL_VAR(long, midi_loop_end);           /* loop when we hit this position */

AL_FUNC(int, detect_midi_driver, (int driver_id));

AL_FUNC(MIDI *, load_midi, (AL_CONST char *filename));
AL_FUNC(void, destroy_midi, (MIDI *midi));
AL_FUNC(int, play_midi, (MIDI *midi, int loop));
AL_FUNC(int, play_looped_midi, (MIDI *midi, int loop_start, int loop_end));
AL_FUNC(void, stop_midi, (void));
AL_FUNC(void, midi_pause, (void));
AL_FUNC(void, midi_resume, (void));
AL_FUNC(int, midi_seek, (int target));
AL_FUNC(int, get_midi_length, (MIDI *midi));
AL_FUNC(void, midi_out, (unsigned char *data, int length));
AL_FUNC(int, load_midi_patches, (void));

AL_FUNCPTR(void, midi_msg_callback, (int msg, int byte1, int byte2));
AL_FUNCPTR(void, midi_meta_callback, (int type, AL_CONST unsigned char *data, int length));
AL_FUNCPTR(void, midi_sysex_callback, (AL_CONST unsigned char *data, int length));

AL_FUNCPTR(void, midi_recorder, (unsigned char data));

AL_FUNC(void, lock_midi, (struct MIDI *midi));

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_MIDI_H */

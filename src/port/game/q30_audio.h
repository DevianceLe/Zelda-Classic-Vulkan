// Music and sound only 2.55 and 3.0 quests have: the quest's music objects (AMUS), the
// music of DMaps and screens, OGG sound effects, sounds from 256 up and tunes from 252
// up, and what scripts call. All of it lives in tables in q30_audio.cpp; a 2.53 quest
// leaves them empty and every hook below does nothing for it.
//
// Functions under "scripts" and "other readers and the game" exist for the code that
// drives them (ffscript wiring, combo triggers and so on).

#ifndef ZC_Q30_AUDIO_H
#define ZC_Q30_AUDIO_H

#include "q255.h"

// Upstream's MAX_SFX. Sounds 1-255 live in customsfxdata, the rest in q30_audio.cpp.
#define Q30_MAX_SFX 1024

// ---- the quest readers ----------------------------------------------------------

// Forget the previous quest's DMap music (with the music object list and the script
// volume and refresh state, which a new quest starts without) and screen music. Called
// by the DMAP and MAP readers; AMUS comes after both of those in a quest file.
void q30_audio_clear_dmaps();
void q30_audio_clear_screens();

// Adds a music object, returns its id (place in the list + 1).
int q30_music_add(const char *name, const char *path, long loop_start, long loop_end,
                  long xfade_in, long xfade_out, byte track, short midi);

// music: a music object's id, 0 for none. 'new_format': the DMap section names music
// objects (upstream's DMAP v25+), so this DMap's midi/tmusic fields are not used.
void q30_set_dmap_music(int dmap, int music, bool new_format);

// Screen 'index' (map*MAPSCRS + screen) plays this music object instead of its DMap's.
void q30_set_screen_music(int index, long music);

// Fills the 2.53 DMap fields of DMaps that name a music object, which link.cpp looks at
// to decide whether a warp keeps the music going. Called after the DMap section and
// after the music object list, whichever is read last.
void q30_finish_music_tables();

void q30_sfx_clear();
// The slot of sound id, in customsfxdata or the high table. NULL when id is past the limit.
SAMPLE *q30_sfx_slot(int id);
// Keeps an OGG sound's file bytes. Decoded when the sound is first played.
void q30_sfx_store_ogg(int id, const byte *data, dword len);
// A free slot below 256 that plays sound 'high', 0 if none is free. For the byte-sized
// fields of screens. q30_sfx_finish_aliases() once the fields are set.
int q30_sfx_alias(int high);
void q30_sfx_finish_aliases();
// q255_maps.cpp: sound ids past 255 in screen fields get their slots, once both the
// map and sound sections are read.
void q255_resolve_high_screen_sfx();

void q30_tunes_clear();
// index counts from the first custom tune; those from MAXCUSTOMTUNES up land here.
void q30_store_tune(int index, const char *title, long start, long loop_start, long loop_end,
                    short loop, short volume, byte flags, byte format, void *midi);

// ---- hooks for zc_sys.cpp -------------------------------------------------------

// True if it decided what plays for the current screen; false: use the 2.53 logic.
bool q30_play_level_music();
// Advances fades and crossfades, once per frame.
void q30_audio_frame();
// music_stop(): also ends a track that was fading out.
void q30_audio_music_stopped();
// music_pause()/music_resume(): a fading old track goes with the current one.
void q30_audio_pause(bool pause);
// The MIDI and enhanced music volumes after Audio->AdjustMusicVolume.
int q30_midi_volume(int volume);
// The sound effect voice volume, after Audio->AdjustSFXVolume.
int q30_sfx_volume(int volume);
// Tune index past MAXMIDIS: plays it (from the high table) and returns true.
bool q30_jukebox_high(int index);

// The sample to allocate a voice for, decoding an OGG sound if it isn't yet. NULL when
// sound id has no sample. id < WAV_COUNT, no OGG: returns customsfxdata (or NULL if that
// has no data and the id is out of range).
const SAMPLE *q30_sfx_sample(int id);
bool q30_sfx_is_ogg(int id);

// ---- scripts --------------------------------------------------------------------
//
// PLAY_MUSIC: 0 stops the music, -1 plays this screen's music, otherwise a music object.
// Also what a combo trigger's "play music" and a string's music command do.
void q30_play_music(long ref);
// PLAYMIDIR/V for quests whose tunes reach past 252 (0 stops). 2.53's own PLAYMIDI still
// works for the first 252.
void q30_play_midi(int midi);
// PLAYENHMUSIC and PLAYENHMUSICEX: filename NULL stops the music, returns whether the
// file was found. track is the script's value minus 1.
bool q30_play_enhanced(const char *filename, int track);
// ENHCROSSFADE: arguments in the order the ZScript call has them (the stack from sp+5 to
// sp): filename or NULL, track, fadeoutframes, fadeinframes, fademiddleframes, startpos.
// Pass the values divided by 10000 except startpos, which stays raw; fades already
// clamped to 0 or more. Returns what the command stores in EXP1 (1 = worked).
bool q30_enh_crossfade(const char *filename, int track, int fadeout, int fadein, int middle, long startpos);
long q30_enh_get_pos();                 // GETENHMUSICPOS
void q30_enh_set_pos(long position);    // SETENHMUSICPOS
void q30_enh_set_speed(long speed);     // SETENHMUSICSPEED (10000 = normal)
long q30_enh_get_length();              // GETENHMUSICLEN
void q30_enh_set_loop(double start, double end); // SETENHMUSICLOOP: seconds (register / 10000.0)

void q30_adjust_music_volume(long perc);        // ADJUSTVOLUMER/V, the register value
void q30_adjust_sfx_volume(long perc);          // ADJUSTSFXVOLUMER/V, the register value

// PLAYSOUNDEX (restart true) and ADJUSTSFX (false). id is divided by 10000; the rest
// are the raw stack values: volume percent, pan, freq in Hz (-1 default), loop.
void q30_sfx_ex(int id, long volume, long pan, long freq, long loop, bool restart);
long q30_sfx_completion(int id);        // GETSFXCOMPLETION (-10000: not playing)

// Audio->... (registers ENGINE_MUSIC_ACTIVE, MUSICUPDATECOND, MUSICUPDATEFLAGS, NUM_MUSICS).
bool q30_engine_music_active();
int q30_music_update_cond();
void q30_set_music_update_cond(int cond);
bool q30_music_update_flag(int index);
void q30_set_music_update_flag(int index, bool on);
int q30_music_count();
int q30_music_by_name(const char *name);        // GETMUSICDATABYNAME: 0 = none
bool q30_music_exists(long ref);                // LOADMUSICDATA: is ref valid

// musicdata:: registers. 'field' uses these names; values are as the register holds them
// (MIDI, track, fades times 10000; loop points raw). ref 0 is valid and reads 0.
enum Q30MusicField
{
    q30md_ID, q30md_IS_ACTIVE, q30md_MIDI, q30md_IS_ENHANCED, q30md_TRACK,
    q30md_LOOPSTART, q30md_LOOPEND, q30md_XFADEIN, q30md_XFADEOUT
};
long q30_musicdata_get(long ref, int field);
void q30_musicdata_set(long ref, int field, long value);
// MUSICDATA_GETPATH/SETPATH/GETNAME/SETNAME. Empty string for ref 0 or an invalid ref.
const char *q30_musicdata_path(long ref);
void q30_musicdata_set_path(long ref, const char *path);
const char *q30_musicdata_name(long ref);
void q30_musicdata_set_name(long ref, const char *name);

// Screen->Music and dmapdata->Music. Getting a screen's music: -1 means "the DMap's".
// Setting a value 0, -1 or a valid object restarts music if it changed and the
// screen or DMap is current and the engine picked the current music.
long q30_screen_music(int index);
void q30_set_screen_music_script(int index, long music);
long q30_dmap_music(int dmap);
void q30_set_dmap_music_script(int dmap, long music);

#endif

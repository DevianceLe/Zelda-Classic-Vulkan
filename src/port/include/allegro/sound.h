/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/sound.h - sound system setup.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_SOUND_H
#define ALLEGRO_SOUND_H

#include "base.h"
#include "digi.h"
#include "stream.h"
#include "midi.h"

#ifdef __cplusplus
   extern "C" {
#endif

AL_FUNC(void, reserve_voices, (int digi_voices, int midi_voices));
AL_FUNC(void, set_volume_per_voice, (int scale));

AL_FUNC(int, install_sound, (int digi, int midi, AL_CONST char *cfg_path));
AL_FUNC(void, remove_sound, (void));

AL_FUNC(int, install_sound_input, (int digi, int midi));
AL_FUNC(void, remove_sound_input, (void));

AL_FUNC(void, set_volume, (int digi_volume, int midi_volume));
AL_FUNC(void, set_hardware_volume, (int digi_volume, int midi_volume));

AL_FUNC(void, get_volume, (int *digi_volume, int *midi_volume));
AL_FUNC(void, get_hardware_volume, (int *digi_volume, int *midi_volume));

AL_FUNC(void, set_mixer_quality, (int quality));
AL_FUNC(int, get_mixer_quality, (void));
AL_FUNC(int, get_mixer_frequency, (void));
AL_FUNC(int, get_mixer_bits, (void));
AL_FUNC(int, get_mixer_channels, (void));
AL_FUNC(int, get_mixer_voices, (void));
AL_FUNC(int, get_mixer_buffer_length, (void));

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_SOUND_H */

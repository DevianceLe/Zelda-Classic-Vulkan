/*
 * zcport: destroy_sample and destroy_midi.
 *
 * Samples and MIDIs come from the datafile loader (malloc'd struct plus a
 * separately malloc'd data block / per-track data) or from game code that
 * builds them with _AL_MALLOC (src/qst.cpp's read_midi). Both use the C
 * allocator, so freeing the same way works for either. Before freeing, the
 * sound layer is told to drop any voice or song still referring to them,
 * like Allegro's destroy functions stop playback.
 *
 * Written from scratch for zcport.
 */

#include <allegro.h>
#include <allegro/internal/aintern.h>

#include "zcport/platform_sound.h"

extern "C" {

void destroy_sample(SAMPLE *spl)
{
    if(!spl)
        return;

    zcport_sound_forget_sample(spl);

    if(spl->data)
        _AL_FREE(spl->data);

    _AL_FREE(spl);
}

SAMPLE *create_sample(int bits, int stereo, int freq, int len)
{
    SAMPLE *spl = (SAMPLE *)_AL_MALLOC(sizeof(SAMPLE));

    if(!spl)
        return NULL;

    spl->bits = bits;
    spl->stereo = stereo;
    spl->freq = freq;
    spl->priority = 128;
    spl->len = len;
    spl->loop_start = 0;
    spl->loop_end = len;
    spl->param = (unsigned long)-1;
    spl->data = _AL_MALLOC((size_t)len * ((bits == 8) ? 1 : 2) * (stereo ? 2 : 1));

    if(!spl->data)
    {
        _AL_FREE(spl);
        return NULL;
    }

    return spl;
}

void destroy_midi(MIDI *midi)
{
    if(!midi)
        return;

    zcport_sound_forget_midi(midi);

    for(int i = 0; i < MIDI_TRACKS; ++i)
    {
        if(midi->track[i].data)
            _AL_FREE(midi->track[i].data);
    }

    _AL_FREE(midi);
}

} // extern "C"

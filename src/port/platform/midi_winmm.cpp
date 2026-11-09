// zcport SDL platform: MIDI output through winmm.
//
// Opens the MIDI mapper, the device the reference build's Windows MIDI driver
// opens, so the music plays on the same synthesizer (normally the Microsoft
// GS Wavetable Synth) with the same messages (the player in
// src/port/al/platform_sound.cpp produces them). No volume is set here: the
// MIDI volume is applied to controller 7 by the player, as in the reference,
// so midiOutSetVolume() never touches the process' audio session.
//
// Android has midi_tsf.cpp; other systems get no backend (silent MIDI).
//
// Written from scratch for zcport (no Allegro code).

#include "platform_sdl_internal.h"
#include "zcport/midi_backend.h"

#ifdef _WIN32

#ifndef NOMINMAX
   #define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

#include <cstring>
#include <vector>

namespace
{

HMIDIOUT g_out = NULL;

int winmm_open(void)
{
   if (g_out)
      return 0;
   HMIDIOUT h = NULL;
   if (midiOutOpen(&h, MIDI_MAPPER, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
      return -1;
   g_out = h;
   return 0;
}

void winmm_close(void)
{
   if (!g_out)
      return;
   midiOutReset(g_out);
   midiOutClose(g_out);
   g_out = NULL;
}

void winmm_short_msg(unsigned int msg)
{
   if (g_out)
      midiOutShortMsg(g_out, (DWORD)msg);
}

void winmm_long_msg(const unsigned char *data, int length)
{
   if (!g_out || !data || length <= 0)
      return;
   std::vector<char> buf(data, data + length);
   MIDIHDR hdr;
   memset(&hdr, 0, sizeof(hdr));
   hdr.lpData = &buf[0];
   hdr.dwBufferLength = (DWORD)length;
   hdr.dwBytesRecorded = (DWORD)length;
   if (midiOutPrepareHeader(g_out, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR)
      return;
   if (midiOutLongMsg(g_out, &hdr, sizeof(hdr)) == MMSYSERR_NOERROR) {
      // Synchronous: the buffer lives on this stack frame.
      for (int i = 0; i < 1000 && !(hdr.dwFlags & MHDR_DONE); i++)
         Sleep(1);
   }
   for (int i = 0; i < 1000; i++) {
      if (midiOutUnprepareHeader(g_out, &hdr, sizeof(hdr)) != MIDIERR_STILLPLAYING)
         break;
      Sleep(1);
   }
}

void winmm_reset(void)
{
   if (g_out)
      midiOutReset(g_out);
}

const ZCPORT_MIDI_BACKEND g_winmm =
{
   "Microsoft MIDI Mapper",
   winmm_open,
   winmm_close,
   winmm_short_msg,
   winmm_long_msg,
   winmm_reset,
   NULL
};

} // namespace

const ZCPORT_MIDI_BACKEND *zcport_sdl::midi_backend()
{
   return &g_winmm;
}

#elif !defined(__ANDROID__)

const ZCPORT_MIDI_BACKEND *zcport_sdl::midi_backend()
{
   return NULL;
}

#endif

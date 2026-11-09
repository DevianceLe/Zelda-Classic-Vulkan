// zplayer: the music decoders behind src/port/game/zcmusic_port.cpp.
//
// This header and zcmusic_decoders.cpp do not include <allegro.h>: the
// decoder headers (dumb.h, Game_Music_Emu, stb_vorbis) clash with its macros
// (ASSERT, TRACE, ...). zcmusic_port.cpp does the zcmusic logic, the file
// reading and the audio streams; this part only turns data into samples.
//
//   OGG Vorbis    stb_vorbis (pushdata API)    replaces alogg
//   MP3           minimp3                      replaces almp3
//   IT/XM/S3M/MOD DUMB 2.0                     replaces DUMB 0.9.3 + aldumb
//   NSF/GBS/SPC/
//   VGM/GYM       Game_Music_Emu 0.6.5         replaces Game_Music_Emu 0.3.0
//
// Written for zcport (no Allegro code).

#ifndef ZCMUSIC_DECODERS_H
#define ZCMUSIC_DECODERS_H


#include <stdint.h>
namespace zcmusic_decoders
{

// ---- OGG and MP3: decoders fed with the file's data blocks -------------------
//
// src/zcmusic.cpp reads OGG and MP3 files in blocks of zcmusic_bufsz KB / 2
// and hands each block to the decoder when it asks for more; these decoders
// take the blocks the same way. Output is signed 16-bit, interleaved,
// channels() samples per frame.
class BlockDecoder
{
public:
    virtual ~BlockDecoder() {}

    // 1 (mono) or 2 (stereo), fixed for the decoder's life.
    virtual int channels() const = 0;
    virtual int rate() const = 0;

    // Appends the file's next block; 'last' when the file ends with it.
    virtual void feed(const void *data, int32_t len, bool last) = 0;

    // True when decode() cannot go on without the next block.
    virtual bool hungry() const = 0;

    // True when everything up to the end of the file has been decoded.
    virtual bool finished() const = 0;

    // Decodes up to 'frames' frames into 'out'. Returns the number of frames
    // written; fewer than asked only when hungry() or finished().
    virtual int32_t decode(short *out, int32_t frames) = 0;

    // Whole-file decoders (open_ogg_seekable, open_mp3_seekable) can do these.
    virtual bool seekable() const { return false; }
    virtual bool seek(int32_t frame) { (void)frame; return false; }
    // Frames in the file; -1 when not known.
    virtual int32_t total_frames() const { return -1; }
};

// The decoder for the first block of an OGG file, as alogg_create_oggstream()
// gets it ('last': the file is that block). NULL when the block does not
// start a Vorbis stream whose headers are all in it (alogg's decoder then
// fails to open the stream as well).
BlockDecoder *open_ogg(const void *data, int32_t len, bool last);

// The decoder for the first MP3 block that src/zcmusic.cpp hands to
// almp3_create_mp3stream(). NULL when no MPEG audio frame decodes from it.
BlockDecoder *open_mp3(const void *data, int32_t len, bool last);

// A decoder over a whole file in memory, for music with loop points and a position
// (2.55 and 3.0 quests). 'data' must stay valid for the decoder's life. NULL when it
// isn't an OGG Vorbis / MP3 file.
BlockDecoder *open_ogg_seekable(const void *data, int32_t len);
BlockDecoder *open_mp3_seekable(const void *data, int32_t len);

// Decodes a whole OGG file: on success '*pcm' is malloc'd signed 16-bit interleaved
// samples ('*channels' per frame). The caller frees it.
bool decode_ogg_sample(const void *data, int32_t len, short **pcm, int *channels, int *rate, int32_t *frames);

// ---- tracker modules (DUMB) -----------------------------------------------------

enum ModuleKind { MODULE_IT, MODULE_XM, MODULE_S3M, MODULE_MOD };

struct Module;          // a loaded module (DUH)
struct ModulePlayer;    // a playing module (DUH_SIGRENDERER)

// dumb_resampling_quality as src/zcmusic.cpp sets it (linear).
void module_init();
// dumb_exit().
void module_exit();

// Loads a module from the file's contents (src/zcmusic.cpp loads through
// DUMB's packfile reader; the caller reads the file with pack_fopen the same
// way). NULL when DUMB does not load it.
Module *module_load(const void *data, int32_t size, ModuleKind kind);
void module_unload(Module *m);

// duh_start_sigrenderer(duh, 0, channels, pos).
ModulePlayer *module_start(Module *m, int channels, int32_t pos);
void module_stop(ModulePlayer *p);
// duh_render(): 'frames' frames of 16-bit unsigned samples at 'freq' Hz,
// scaled by 'volume'. Returns the frames rendered (0 when the module ended).
int32_t module_render(ModulePlayer *p, float volume, int freq, int32_t frames, void *out);
// The module's length in 1/65536 s (worked out once; the first call can take a moment).
// 0 when not known.
int32_t module_length(Module *m);

// ---- Game_Music_Emu -------------------------------------------------------------

struct Emu;

// As src/zcmusic.cpp's gme_load_file(): the emulator chosen by the extension
// ("spc", "gbs", "vgm", "nsf", "gym"), set to 'rate' Hz, the file opened
// with stdio. NULL when it does not load.
Emu *emu_load(const char *filename, const char *ext, int32_t rate);
void emu_unload(Emu *e);
int emu_track_count(Emu *e);
void emu_start_track(Emu *e, int track);
// Music_Emu::play(): 'count' samples (stereo pairs, so count is even),
// signed 16-bit.
void emu_play(Emu *e, int32_t count, short *out);

} // namespace zcmusic_decoders

#endif // ZCMUSIC_DECODERS_H

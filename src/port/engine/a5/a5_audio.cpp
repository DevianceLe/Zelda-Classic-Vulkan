// Sound for the unified engine, from scratch, on zcport's voices and mixer. Sample
// data is converted to zcport's format when first played; streams (OGG, MP3, WAV)
// are decoded in full when loaded, then played the same way.
#include <allegro5/allegro.h>
#include <a5alleg.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static bool g_audio_installed = false;

bool al_install_audio()
{
	// The offscreen check has no platform, so no audio device either.
	g_audio_installed = getenv("ZC_ENGINE_OFFSCREEN") == nullptr;
	return g_audio_installed;
}
bool al_is_audio_installed() { return g_audio_installed; }

struct ALLEGRO_MIXER { int unused; };
ALLEGRO_MIXER* al_get_default_mixer() { static ALLEGRO_MIXER m; return &m; }
bool al_reserve_samples(int) { return true; }
unsigned al_get_channel_count(ALLEGRO_CHANNEL_CONF conf) { return (conf >> 4) + (conf & 0xF); }
size_t al_get_audio_depth_size(ALLEGRO_AUDIO_DEPTH depth)
{
	switch (depth & ~ALLEGRO_AUDIO_DEPTH_UNSIGNED)
	{
		case ALLEGRO_AUDIO_DEPTH_INT8: return 1;
		case ALLEGRO_AUDIO_DEPTH_INT16: return 2;
		case ALLEGRO_AUDIO_DEPTH_INT24: return 3;
		default: return 4;
	}
}

// ---- samples -----------------------------------------------------------------
struct ALLEGRO_SAMPLE
{
	void* buf;
	unsigned samples, freq;
	ALLEGRO_AUDIO_DEPTH depth;
	ALLEGRO_CHANNEL_CONF conf;
	bool free_buf;
	SAMPLE* voice_data;
};

ALLEGRO_SAMPLE* al_create_sample(void* buf, unsigned samples, unsigned freq, ALLEGRO_AUDIO_DEPTH depth, ALLEGRO_CHANNEL_CONF conf, bool free_buf)
{
	return new ALLEGRO_SAMPLE{buf, samples, freq, depth, conf, free_buf, nullptr};
}
void al_destroy_sample(ALLEGRO_SAMPLE* s)
{
	if (!s)
		return;
	if (s->voice_data)
		destroy_sample(s->voice_data);
	if (s->free_buf)
		al_free(s->buf);
	delete s;
}
ALLEGRO_CHANNEL_CONF al_get_sample_channels(const ALLEGRO_SAMPLE* s) { return s->conf; }
void* al_get_sample_data(const ALLEGRO_SAMPLE* s) { return s->buf; }
ALLEGRO_AUDIO_DEPTH al_get_sample_depth(const ALLEGRO_SAMPLE* s) { return s->depth; }
unsigned al_get_sample_frequency(const ALLEGRO_SAMPLE* s) { return s->freq; }
unsigned al_get_sample_length(const ALLEGRO_SAMPLE* s) { return s->samples; }

// zcport voices play unsigned 8 or 16 bit data.
static SAMPLE* voice_data_of(ALLEGRO_SAMPLE* s)
{
	if (s->voice_data)
		return s->voice_data;
	const unsigned channels = std::min(al_get_channel_count(s->conf), 2u);
	const unsigned source_channels = al_get_channel_count(s->conf);
	const size_t source_size = al_get_audio_depth_size(s->depth);
	const bool is_unsigned = (s->depth & ALLEGRO_AUDIO_DEPTH_UNSIGNED) != 0;
	const int base = s->depth & ~ALLEGRO_AUDIO_DEPTH_UNSIGNED;
	const int bits = base == ALLEGRO_AUDIO_DEPTH_INT8 ? 8 : 16;
	SAMPLE* out = create_sample(bits, channels == 2, s->freq, s->samples);
	if (!out)
		return nullptr;
	const unsigned char* in = (const unsigned char*)s->buf;
	for (size_t i = 0; i < s->samples; i++)
	{
		for (unsigned c = 0; c < channels; c++)
		{
			const unsigned char* p = in + (i * source_channels + c) * source_size;
			if (bits == 8)
			{
				((uint8_t*)out->data)[i * channels + c] = is_unsigned ? p[0] : (uint8_t)(p[0] ^ 0x80);
				continue;
			}
			int v;
			if (base == ALLEGRO_AUDIO_DEPTH_INT16)
				v = (int16_t)(p[0] | (p[1] << 8));
			else if (base == ALLEGRO_AUDIO_DEPTH_INT24)
				v = (int16_t)(p[1] | (p[2] << 8));
			else
			{
				float f;
				std::memcpy(&f, p, 4);
				v = (int)(std::min(1.0f, std::max(-1.0f, f)) * 32767.0f);
			}
			if (is_unsigned && base != ALLEGRO_AUDIO_DEPTH_FLOAT32)
				v ^= 0x8000;
			((uint16_t*)out->data)[i * channels + c] = (uint16_t)(v ^ 0x8000);
		}
	}
	s->voice_data = out;
	return out;
}

// ---- playing a sample --------------------------------------------------------
struct ALLEGRO_SAMPLE_INSTANCE
{
	ALLEGRO_SAMPLE* sample;
	int voice;
	float gain, pan, speed;
	ALLEGRO_PLAYMODE mode;
	unsigned pos;
};

static void apply_settings(ALLEGRO_SAMPLE_INSTANCE* i)
{
	if (i->voice < 0)
		return;
	voice_set_volume(i->voice, std::clamp((int)(i->gain * 255.0f), 0, 255));
	// Values outside -1..1 mean "no panning".
	voice_set_pan(i->voice, (i->pan < -1.0f || i->pan > 1.0f) ? 128 : std::clamp((int)((i->pan + 1.0f) * 127.5f), 0, 255));
	voice_set_frequency(i->voice, (int)(i->sample->freq * i->speed));
	voice_set_playmode(i->voice, i->mode == ALLEGRO_PLAYMODE_LOOP ? PLAYMODE_LOOP : PLAYMODE_PLAY);
}
static void release_voice(ALLEGRO_SAMPLE_INSTANCE* i)
{
	if (i->voice >= 0)
	{
		deallocate_voice(i->voice);
		i->voice = -1;
	}
}
ALLEGRO_SAMPLE_INSTANCE* al_create_sample_instance(ALLEGRO_SAMPLE* s) { return new ALLEGRO_SAMPLE_INSTANCE{s, -1, 1.0f, 0.0f, 1.0f, ALLEGRO_PLAYMODE_ONCE, 0}; }
void al_destroy_sample_instance(ALLEGRO_SAMPLE_INSTANCE* i)
{
	if (!i)
		return;
	release_voice(i);
	delete i;
}
bool al_attach_sample_instance_to_mixer(ALLEGRO_SAMPLE_INSTANCE*, ALLEGRO_MIXER*) { return true; }
bool al_get_sample_instance_playing(const ALLEGRO_SAMPLE_INSTANCE* i) { return i->voice >= 0 && voice_get_position(i->voice) >= 0; }
bool al_play_sample_instance(ALLEGRO_SAMPLE_INSTANCE* i)
{
	if (!g_audio_installed)
		return false;
	SAMPLE* data = voice_data_of(i->sample);
	if (!data)
		return false;
	release_voice(i);
	i->voice = allocate_voice(data);
	if (i->voice < 0)
		return false;
	apply_settings(i);
	voice_set_position(i->voice, i->pos);
	voice_start(i->voice);
	return true;
}
bool al_stop_sample_instance(ALLEGRO_SAMPLE_INSTANCE* i)
{
	if (i->voice >= 0)
	{
		int p = voice_get_position(i->voice);
		i->pos = p >= 0 ? (unsigned)p : 0;
		release_voice(i);
	}
	return true;
}
unsigned al_get_sample_instance_position(const ALLEGRO_SAMPLE_INSTANCE* i)
{
	if (i->voice >= 0)
	{
		int p = voice_get_position(i->voice);
		if (p >= 0)
			return (unsigned)p;
	}
	return i->pos;
}
bool al_set_sample_instance_gain(ALLEGRO_SAMPLE_INSTANCE* i, float gain) { i->gain = gain; apply_settings(i); return true; }
bool al_set_sample_instance_pan(ALLEGRO_SAMPLE_INSTANCE* i, float pan) { i->pan = pan; apply_settings(i); return true; }
bool al_set_sample_instance_speed(ALLEGRO_SAMPLE_INSTANCE* i, float speed) { i->speed = speed; apply_settings(i); return true; }
bool al_set_sample_instance_playmode(ALLEGRO_SAMPLE_INSTANCE* i, ALLEGRO_PLAYMODE mode) { i->mode = mode; apply_settings(i); return true; }
bool al_set_sample_instance_position(ALLEGRO_SAMPLE_INSTANCE* i, unsigned pos)
{
	i->pos = pos;
	if (i->voice >= 0)
		voice_set_position(i->voice, (int)pos);
	return true;
}

// ---- decoding ----------------------------------------------------------------
static ALLEGRO_SAMPLE* sample_from_s16(const int16_t* pcm, size_t frames, int channels, int rate)
{
	const size_t bytes = frames * channels * 2;
	void* copy = al_malloc(bytes);
	if (!copy)
		return nullptr;
	std::memcpy(copy, pcm, bytes);
	return al_create_sample(copy, (unsigned)frames, rate, ALLEGRO_AUDIO_DEPTH_INT16, channels == 1 ? ALLEGRO_CHANNEL_CONF_1 : ALLEGRO_CHANNEL_CONF_2, true);
}
static ALLEGRO_SAMPLE* decode_ogg(const std::vector<unsigned char>& d)
{
	int channels = 0, rate = 0;
	short* out = nullptr;
	int frames = stb_vorbis_decode_memory(d.data(), (int)d.size(), &channels, &rate, &out);
	if (frames <= 0 || !out)
		return nullptr;
	ALLEGRO_SAMPLE* s = nullptr;
	if (channels <= 2)
		s = sample_from_s16(out, frames, channels, rate);
	free(out);
	return s;
}
static ALLEGRO_SAMPLE* decode_mp3(const std::vector<unsigned char>& d)
{
	static mp3dec_t dec;
	mp3dec_init(&dec);
	std::vector<int16_t> pcm;
	int channels = 0, rate = 0;
	size_t at = 0;
	mp3d_sample_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME];
	while (at < d.size())
	{
		mp3dec_frame_info_t info;
		int n = mp3dec_decode_frame(&dec, d.data() + at, (int)(d.size() - at), frame, &info);
		if (info.frame_bytes == 0)
			break;
		at += info.frame_bytes;
		if (n > 0)
		{
			channels = info.channels;
			rate = info.hz;
			pcm.insert(pcm.end(), frame, frame + n * info.channels);
		}
	}
	if (pcm.empty() || channels < 1 || channels > 2)
		return nullptr;
	return sample_from_s16(pcm.data(), pcm.size() / channels, channels, rate);
}
static ALLEGRO_SAMPLE* decode_wav(const std::vector<unsigned char>& d)
{
	if (d.size() < 12 || memcmp(d.data(), "RIFF", 4) || memcmp(d.data() + 8, "WAVE", 4))
		return nullptr;
	int channels = 0, rate = 0, bits = 0;
	size_t at = 12;
	while (at + 8 <= d.size())
	{
		uint32_t len = d[at + 4] | (d[at + 5] << 8) | (d[at + 6] << 16) | ((uint32_t)d[at + 7] << 24);
		const unsigned char* body = d.data() + at + 8;
		if (!memcmp(d.data() + at, "fmt ", 4) && len >= 16)
		{
			channels = body[2] | (body[3] << 8);
			rate = body[4] | (body[5] << 8) | (body[6] << 16) | ((uint32_t)body[7] << 24);
			bits = body[14] | (body[15] << 8);
		}
		else if (!memcmp(d.data() + at, "data", 4) && channels && bits)
		{
			len = (uint32_t)std::min<size_t>(len, d.size() - at - 8);
			const size_t frames = len / (channels * bits / 8);
			if ((bits != 8 && bits != 16) || channels > 2)
				return nullptr;
			void* copy = al_malloc(len);
			std::memcpy(copy, body, len);
			return al_create_sample(copy, (unsigned)frames, rate,
				bits == 8 ? (ALLEGRO_AUDIO_DEPTH)(ALLEGRO_AUDIO_DEPTH_INT8 | ALLEGRO_AUDIO_DEPTH_UNSIGNED) : ALLEGRO_AUDIO_DEPTH_INT16,
				channels == 1 ? ALLEGRO_CHANNEL_CONF_1 : ALLEGRO_CHANNEL_CONF_2, true);
		}
		at += 8 + (size_t)len + (len & 1);
	}
	return nullptr;
}
static ALLEGRO_SAMPLE* decode(const std::vector<unsigned char>& data, std::string ext)
{
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
	if (ext == ".ogg")
		return decode_ogg(data);
	if (ext == ".mp3")
		return decode_mp3(data);
	if (ext == ".wav")
		return decode_wav(data);
	return nullptr;
}
static std::string extension_of(const char* path)
{
	const char* dot = strrchr(path, '.');
	return dot ? dot : "";
}
static bool read_all(const char* path, std::vector<unsigned char>& out)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return true;
}
static std::vector<unsigned char> read_rest(ALLEGRO_FILE* f)
{
	std::vector<unsigned char> data((size_t)al_fsize(f));
	data.resize(al_fread(f, data.data(), data.size()));
	return data;
}

ALLEGRO_SAMPLE* al_load_sample(const char* path)
{
	std::vector<unsigned char> data;
	return read_all(path, data) ? decode(data, extension_of(path)) : nullptr;
}
ALLEGRO_SAMPLE* al_load_sample_f(ALLEGRO_FILE* f, const char* ident) { return decode(read_rest(f), ident); }
bool al_save_sample(const char* path, ALLEGRO_SAMPLE* s)
{
	const unsigned channels = al_get_channel_count(s->conf);
	const bool eight = (s->depth & ~ALLEGRO_AUDIO_DEPTH_UNSIGNED) == ALLEGRO_AUDIO_DEPTH_INT8;
	if (!eight && (s->depth & ~ALLEGRO_AUDIO_DEPTH_UNSIGNED) != ALLEGRO_AUDIO_DEPTH_INT16)
		return false;
	const uint32_t bytes_per = eight ? 1 : 2, data_len = s->samples * channels * bytes_per;
	std::ofstream out(path, std::ios::binary);
	if (!out)
		return false;
	auto u32 = [&](uint32_t v) { out.write((const char*)&v, 4); };
	auto u16 = [&](uint16_t v) { out.write((const char*)&v, 2); };
	out.write("RIFF", 4); u32(36 + data_len); out.write("WAVEfmt ", 8); u32(16); u16(1); u16((uint16_t)channels);
	u32(s->freq); u32(s->freq * channels * bytes_per); u16((uint16_t)(channels * bytes_per)); u16((uint16_t)(bytes_per * 8));
	out.write("data", 4); u32(data_len);
	if (eight && !(s->depth & ALLEGRO_AUDIO_DEPTH_UNSIGNED))
	{
		for (uint32_t i = 0; i < data_len; i++)
			out.put((char)(((const unsigned char*)s->buf)[i] ^ 0x80));
	}
	else
		out.write((const char*)s->buf, data_len);
	return (bool)out;
}

// ---- streams: a fully decoded sample with its own playing instance -----------
struct ALLEGRO_AUDIO_STREAM
{
	ALLEGRO_SAMPLE* sample;
	ALLEGRO_SAMPLE_INSTANCE* instance;
	ALLEGRO_FILE* file;
};

static ALLEGRO_AUDIO_STREAM* stream_from(ALLEGRO_SAMPLE* s, ALLEGRO_FILE* file)
{
	if (!s)
		return nullptr;
	return new ALLEGRO_AUDIO_STREAM{s, al_create_sample_instance(s), file};
}
ALLEGRO_AUDIO_STREAM* al_load_audio_stream(const char* path, size_t, unsigned) { return stream_from(al_load_sample(path), nullptr); }
ALLEGRO_AUDIO_STREAM* al_load_audio_stream_f(ALLEGRO_FILE* f, const char* ident, size_t, unsigned) { return stream_from(al_load_sample_f(f, ident), f); }
void al_destroy_audio_stream(ALLEGRO_AUDIO_STREAM* s)
{
	if (!s)
		return;
	al_destroy_sample_instance(s->instance);
	al_destroy_sample(s->sample);
	if (s->file)
		al_fclose(s->file); // a stream owns the file it was loaded from
	delete s;
}
bool al_attach_audio_stream_to_mixer(ALLEGRO_AUDIO_STREAM*, ALLEGRO_MIXER*) { return true; }
unsigned al_get_audio_stream_frequency(const ALLEGRO_AUDIO_STREAM* s) { return s->sample->freq; }
double al_get_audio_stream_length_secs(ALLEGRO_AUDIO_STREAM* s) { return double(s->sample->samples) / s->sample->freq; }
double al_get_audio_stream_position_secs(ALLEGRO_AUDIO_STREAM* s) { return double(al_get_sample_instance_position(s->instance)) / s->sample->freq; }
bool al_get_audio_stream_playing(const ALLEGRO_AUDIO_STREAM* s) { return al_get_sample_instance_playing(s->instance); }
bool al_seek_audio_stream_secs(ALLEGRO_AUDIO_STREAM* s, double t)
{
	return al_set_sample_instance_position(s->instance, (unsigned)(std::max(0.0, t) * s->sample->freq));
}
bool al_set_audio_stream_gain(ALLEGRO_AUDIO_STREAM* s, float gain) { return al_set_sample_instance_gain(s->instance, gain); }
bool al_set_audio_stream_pan(ALLEGRO_AUDIO_STREAM* s, float pan) { return al_set_sample_instance_pan(s->instance, pan); }
bool al_set_audio_stream_speed(ALLEGRO_AUDIO_STREAM* s, float speed) { return al_set_sample_instance_speed(s->instance, speed); }
bool al_set_audio_stream_playmode(ALLEGRO_AUDIO_STREAM* s, ALLEGRO_PLAYMODE mode) { return al_set_sample_instance_playmode(s->instance, mode); }
bool al_set_audio_stream_playing(ALLEGRO_AUDIO_STREAM* s, bool playing)
{
	return playing ? al_play_sample_instance(s->instance) : al_stop_sample_instance(s->instance);
}
bool al_set_audio_stream_loop_secs(ALLEGRO_AUDIO_STREAM* s, double start, double end)
{
	SAMPLE* data = voice_data_of(s->sample);
	if (!data)
		return false;
	data->loop_start = (unsigned long)(std::max(0.0, start) * s->sample->freq);
	data->loop_end = (unsigned long)std::min<double>(end * s->sample->freq, s->sample->samples);
	return true;
}

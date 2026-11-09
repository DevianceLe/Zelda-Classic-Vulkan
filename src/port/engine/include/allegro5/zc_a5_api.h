// The part of the Allegro 5 API the unified engine calls, written from scratch
// for zcport: only the names and types the engine source uses. The functions are
// implemented in src/port/engine/a5/.
#ifndef ZC_A5_API_H
#define ZC_A5_API_H

#include <cstdint>
#include <cstddef>
#include <cstdio>

struct ALLEGRO_BITMAP;
struct ALLEGRO_DISPLAY;
struct ALLEGRO_EVENT_QUEUE;
struct ALLEGRO_EVENT_SOURCE;
struct ALLEGRO_FONT;
struct ALLEGRO_MOUSE_CURSOR;
struct ALLEGRO_JOYSTICK;
struct ALLEGRO_CONFIG;
struct ALLEGRO_CONFIG_SECTION;
struct ALLEGRO_CONFIG_ENTRY;
struct ALLEGRO_FILE;
struct ALLEGRO_FS_ENTRY;
struct ALLEGRO_THREAD;
struct ALLEGRO_MUTEX;
struct ALLEGRO_COND;
struct ALLEGRO_SHADER;
struct ALLEGRO_TEXTLOG;
struct ALLEGRO_AUDIO_STREAM;
struct ALLEGRO_SAMPLE;
struct ALLEGRO_SAMPLE_INSTANCE;
struct ALLEGRO_MIXER;

struct ALLEGRO_COLOR { float r, g, b, a; };

struct ALLEGRO_LOCKED_REGION
{
	void* data;
	int format;
	int pitch;
	int pixel_size;
};

struct ALLEGRO_MONITOR_INFO { int x1, y1, x2, y2; };
struct ALLEGRO_JOYSTICK_GUID { uint8_t val[16]; };

// Saved by al_store_state: the fields the port tracks per caller.
struct ALLEGRO_STATE
{
	int flags;
	ALLEGRO_BITMAP* target;
	int new_bitmap_flags;
	int blend_op, blend_src, blend_dst;
};

enum
{
	ALLEGRO_STATE_NEW_BITMAP_PARAMETERS = 0x0002,
	ALLEGRO_STATE_BLENDER = 0x0010,
	ALLEGRO_STATE_TARGET_BITMAP = 0x0080,
};

enum
{
	ALLEGRO_MEMORY_BITMAP = 0x0001,
	ALLEGRO_NO_PRESERVE_TEXTURE = 0x0008,
	ALLEGRO_MIN_LINEAR = 0x0040,
	ALLEGRO_MAG_LINEAR = 0x0080,
	ALLEGRO_CONVERT_BITMAP = 0x1000,
};

enum
{
	ALLEGRO_PIXEL_FORMAT_ANY = 0,
	ALLEGRO_PIXEL_FORMAT_ARGB_8888 = 9,
	ALLEGRO_PIXEL_FORMAT_RGBA_8888 = 10,
	ALLEGRO_PIXEL_FORMAT_ABGR_8888 = 17,
	ALLEGRO_PIXEL_FORMAT_ABGR_8888_LE = 25,
};

enum
{
	ALLEGRO_LOCK_READWRITE = 0,
	ALLEGRO_LOCK_READONLY = 1,
	ALLEGRO_LOCK_WRITEONLY = 2,
};

enum { ALLEGRO_ADD = 0 };
enum { ALLEGRO_ZERO = 0, ALLEGRO_ONE = 1, ALLEGRO_ALPHA = 2, ALLEGRO_INVERSE_ALPHA = 3 };

enum
{
	ALLEGRO_RESIZABLE = 0x0010,
	ALLEGRO_PROGRAMMABLE_PIPELINE = 0x0800,
};

enum { ALLEGRO_AUTO_CONVERT_BITMAPS = 34, ALLEGRO_VSYNC = 26 };
enum { ALLEGRO_DONTCARE = 0, ALLEGRO_REQUIRE = 1, ALLEGRO_SUGGEST = 2 };
enum { ALLEGRO_FILEMODE_ISDIR = 32 };

enum
{
	ALLEGRO_EVENT_KEY_DOWN = 10,
	ALLEGRO_EVENT_KEY_UP = 12,
	ALLEGRO_EVENT_DISPLAY_RESIZE = 41,
	ALLEGRO_EVENT_DISPLAY_CLOSE = 42,
};

enum { ALLEGRO_KEY_F5 = 51, ALLEGRO_KEY_F6 = 52, ALLEGRO_KEY_F10 = 56, ALLEGRO_KEY_F11 = 57 };

struct ALLEGRO_DISPLAY_EVENT { int type; void* source; double timestamp; int x, y, width, height; };
struct ALLEGRO_KEYBOARD_EVENT { int type; void* source; double timestamp; ALLEGRO_DISPLAY* display; int keycode; int unichar; unsigned modifiers; bool repeat; };
union ALLEGRO_EVENT
{
	int type;
	ALLEGRO_DISPLAY_EVENT display;
	ALLEGRO_KEYBOARD_EVENT keyboard;
};

enum ALLEGRO_JOYSTICK_TYPE { ALLEGRO_JOYSTICK_TYPE_UNKNOWN = 0, ALLEGRO_JOYSTICK_TYPE_GAMEPAD = 1 };
enum { ALLEGRO_GAMEPAD_STICK_LEFT_THUMB = 0, ALLEGRO_GAMEPAD_STICK_RIGHT_THUMB = 1 };

enum { ALLEGRO_SYSTEM_MOUSE_CURSOR_LINK = 17 };

enum ALLEGRO_SHADER_PLATFORM
{
	ALLEGRO_SHADER_AUTO = 0,
	ALLEGRO_SHADER_GLSL = 1,
	ALLEGRO_SHADER_HLSL = 2,
	ALLEGRO_SHADER_HLSL_SM_3_0 = 8,
};
enum ALLEGRO_SHADER_TYPE { ALLEGRO_VERTEX_SHADER = 1, ALLEGRO_PIXEL_SHADER = 2 };

enum { ALLEGRO_MESSAGEBOX_ERROR = 1, ALLEGRO_MESSAGEBOX_YES_NO = 4, ALLEGRO_MESSAGEBOX_OK_CANCEL = 8 };
enum { ALLEGRO_TEXTLOG_MONOSPACE = 2 };

enum ALLEGRO_AUDIO_DEPTH
{
	ALLEGRO_AUDIO_DEPTH_INT8 = 0,
	ALLEGRO_AUDIO_DEPTH_INT16 = 1,
	ALLEGRO_AUDIO_DEPTH_INT24 = 2,
	ALLEGRO_AUDIO_DEPTH_FLOAT32 = 3,
	ALLEGRO_AUDIO_DEPTH_UNSIGNED = 8,
};
enum ALLEGRO_CHANNEL_CONF { ALLEGRO_CHANNEL_CONF_1 = 0x10, ALLEGRO_CHANNEL_CONF_2 = 0x20 };
enum ALLEGRO_PLAYMODE { ALLEGRO_PLAYMODE_ONCE = 0x100, ALLEGRO_PLAYMODE_LOOP = 0x101 };

// System and add-ons
bool al_init();
bool al_init_font_addon();
bool al_init_image_addon();
bool al_init_primitives_addon();
bool al_init_acodec_addon();
bool al_install_audio();
bool al_is_audio_installed();
void al_register_trace_handler(void (*handler)(const char*));
double al_get_time();
void* al_malloc(size_t n);
void al_free(void* p);

// Colors
ALLEGRO_COLOR al_map_rgb(unsigned char r, unsigned char g, unsigned char b);
ALLEGRO_COLOR al_map_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
ALLEGRO_COLOR al_map_rgb_f(float r, float g, float b);
ALLEGRO_COLOR al_map_rgba_f(float r, float g, float b, float a);
ALLEGRO_COLOR al_premul_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
ALLEGRO_COLOR al_premul_rgba_f(float r, float g, float b, float a);
void al_unmap_rgb(ALLEGRO_COLOR c, unsigned char* r, unsigned char* g, unsigned char* b);
void al_unmap_rgba(ALLEGRO_COLOR c, unsigned char* r, unsigned char* g, unsigned char* b, unsigned char* a);

// Bitmaps
ALLEGRO_BITMAP* al_create_bitmap(int w, int h);
void al_destroy_bitmap(ALLEGRO_BITMAP* b);
int al_get_bitmap_width(ALLEGRO_BITMAP* b);
int al_get_bitmap_height(ALLEGRO_BITMAP* b);
int al_get_bitmap_flags(ALLEGRO_BITMAP* b);
int al_get_bitmap_format(ALLEGRO_BITMAP* b);
int al_get_new_bitmap_flags();
void al_set_new_bitmap_flags(int flags);
ALLEGRO_LOCKED_REGION* al_lock_bitmap(ALLEGRO_BITMAP* b, int format, int flags);
void al_unlock_bitmap(ALLEGRO_BITMAP* b);
void al_put_pixel(int x, int y, ALLEGRO_COLOR c);
void al_set_target_bitmap(ALLEGRO_BITMAP* b);
void al_set_target_backbuffer(ALLEGRO_DISPLAY* d);
ALLEGRO_BITMAP* al_get_target_bitmap();
ALLEGRO_BITMAP* al_get_backbuffer(ALLEGRO_DISPLAY* d);
void al_clear_to_color(ALLEGRO_COLOR c);
void al_draw_bitmap(ALLEGRO_BITMAP* b, float dx, float dy, int flags);
void al_draw_scaled_bitmap(ALLEGRO_BITMAP* b, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, int flags);
void al_draw_tinted_scaled_bitmap(ALLEGRO_BITMAP* b, ALLEGRO_COLOR tint, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, int flags);
void al_set_blender(int op, int src, int dst);
void al_set_clipping_rectangle(int x, int y, int w, int h);
void al_get_clipping_rectangle(int* x, int* y, int* w, int* h);
void al_reset_clipping_rectangle();
void al_store_state(ALLEGRO_STATE* s, int flags);
void al_restore_state(const ALLEGRO_STATE* s);
ALLEGRO_BITMAP* al_load_bitmap(const char* path);
bool al_save_bitmap(const char* path, ALLEGRO_BITMAP* b);

// Primitives and text
void al_draw_line(float x1, float y1, float x2, float y2, ALLEGRO_COLOR c, float thickness);
void al_draw_rectangle(float x1, float y1, float x2, float y2, ALLEGRO_COLOR c, float thickness);
void al_draw_filled_rectangle(float x1, float y1, float x2, float y2, ALLEGRO_COLOR c);
void al_draw_circle(float cx, float cy, float r, ALLEGRO_COLOR c, float thickness);
void al_draw_filled_circle(float cx, float cy, float r, ALLEGRO_COLOR c);
void al_draw_arc(float cx, float cy, float r, float start, float delta, ALLEGRO_COLOR c, float thickness);
void al_draw_text(const ALLEGRO_FONT* f, ALLEGRO_COLOR c, float x, float y, int flags, const char* text);
void al_destroy_font(ALLEGRO_FONT* f);
int al_get_font_line_height(const ALLEGRO_FONT* f);
int al_get_text_width(const ALLEGRO_FONT* f, const char* text);
ALLEGRO_FONT* al_grab_font_from_bitmap(ALLEGRO_BITMAP* b, int n, const int ranges[]);

// Display, window and clipboard
ALLEGRO_DISPLAY* al_create_display(int w, int h);
void al_destroy_display(ALLEGRO_DISPLAY* d);
void al_flip_display();
int al_get_display_width(ALLEGRO_DISPLAY* d);
int al_get_display_height(ALLEGRO_DISPLAY* d);
bool al_resize_display(ALLEGRO_DISPLAY* d, int w, int h);
bool al_acknowledge_resize(ALLEGRO_DISPLAY* d);
ALLEGRO_EVENT_SOURCE* al_get_display_event_source(ALLEGRO_DISPLAY* d);
void al_set_window_position(ALLEGRO_DISPLAY* d, int x, int y);
void al_get_window_position(ALLEGRO_DISPLAY* d, int* x, int* y);
void al_set_window_title(ALLEGRO_DISPLAY* d, const char* title);
void al_set_display_icon(ALLEGRO_DISPLAY* d, ALLEGRO_BITMAP* icon);
void al_set_new_display_flags(int flags);
void al_set_new_display_option(int option, int value, int importance);
bool al_get_monitor_info(int adapter, ALLEGRO_MONITOR_INFO* info);
int al_get_monitor_dpi(int adapter);
bool al_clipboard_has_text(ALLEGRO_DISPLAY* d);
char* al_get_clipboard_text(ALLEGRO_DISPLAY* d);
bool al_set_clipboard_text(ALLEGRO_DISPLAY* d, const char* text);

// Mouse, keyboard, events
ALLEGRO_MOUSE_CURSOR* al_create_mouse_cursor(ALLEGRO_BITMAP* b, int x, int y);
void al_destroy_mouse_cursor(ALLEGRO_MOUSE_CURSOR* c);
bool al_set_mouse_cursor(ALLEGRO_DISPLAY* d, ALLEGRO_MOUSE_CURSOR* c);
bool al_set_system_mouse_cursor(ALLEGRO_DISPLAY* d, int id);
bool al_show_mouse_cursor(ALLEGRO_DISPLAY* d);
bool al_is_mouse_installed();
ALLEGRO_EVENT_SOURCE* al_get_mouse_event_source();
ALLEGRO_EVENT_SOURCE* al_get_keyboard_event_source();
ALLEGRO_EVENT_QUEUE* al_create_event_queue();
void al_destroy_event_queue(ALLEGRO_EVENT_QUEUE* q);
void al_register_event_source(ALLEGRO_EVENT_QUEUE* q, ALLEGRO_EVENT_SOURCE* s);
bool al_get_next_event(ALLEGRO_EVENT_QUEUE* q, ALLEGRO_EVENT* e);

// Joysticks
int al_get_num_joysticks();
ALLEGRO_JOYSTICK* al_get_joystick(int i);
bool al_get_joystick_active(ALLEGRO_JOYSTICK* j);
ALLEGRO_JOYSTICK_GUID al_get_joystick_guid(ALLEGRO_JOYSTICK* j);
const char* al_get_joystick_name(ALLEGRO_JOYSTICK* j);
int al_get_joystick_num_sticks(ALLEGRO_JOYSTICK* j);
int al_get_joystick_num_buttons(ALLEGRO_JOYSTICK* j);
ALLEGRO_JOYSTICK_TYPE al_get_joystick_type(ALLEGRO_JOYSTICK* j);
bool al_set_joystick_mappings(const char* path);

// Config
ALLEGRO_CONFIG* al_create_config();
void al_destroy_config(ALLEGRO_CONFIG* c);
ALLEGRO_CONFIG* al_load_config_file(const char* path);
bool al_save_config_file(const char* path, const ALLEGRO_CONFIG* c);
const char* al_get_config_value(const ALLEGRO_CONFIG* c, const char* section, const char* key);
void al_set_config_value(ALLEGRO_CONFIG* c, const char* section, const char* key, const char* value);
void al_add_config_comment(ALLEGRO_CONFIG* c, const char* section, const char* comment);
void al_add_config_section(ALLEGRO_CONFIG* c, const char* section);
bool al_remove_config_key(ALLEGRO_CONFIG* c, const char* section, const char* key);
bool al_remove_config_section(ALLEGRO_CONFIG* c, const char* section);
const char* al_get_first_config_section(const ALLEGRO_CONFIG* c, ALLEGRO_CONFIG_SECTION** it);
const char* al_get_next_config_section(ALLEGRO_CONFIG_SECTION** it);
const char* al_get_first_config_entry(const ALLEGRO_CONFIG* c, const char* section, ALLEGRO_CONFIG_ENTRY** it);
const char* al_get_next_config_entry(ALLEGRO_CONFIG_ENTRY** it);
void al_merge_config_into(ALLEGRO_CONFIG* master, const ALLEGRO_CONFIG* add);
ALLEGRO_CONFIG* al_get_system_config();

// Files
ALLEGRO_FILE* al_fopen(const char* path, const char* mode);
bool al_fclose(ALLEGRO_FILE* f);
bool al_fflush(ALLEGRO_FILE* f);
int al_fputs(ALLEGRO_FILE* f, const char* s);
size_t al_fread(ALLEGRO_FILE* f, void* p, size_t n);
size_t al_fwrite(ALLEGRO_FILE* f, const void* p, size_t n);
int64_t al_fsize(ALLEGRO_FILE* f);
ALLEGRO_FILE* al_open_memfile(void* mem, int64_t size, const char* mode);
bool al_filename_exists(const char* path);
bool al_make_directory(const char* path);
ALLEGRO_FS_ENTRY* al_create_fs_entry(const char* path);
void al_destroy_fs_entry(ALLEGRO_FS_ENTRY* e);
uint32_t al_get_fs_entry_mode(ALLEGRO_FS_ENTRY* e);
const char* al_get_fs_entry_name(ALLEGRO_FS_ENTRY* e);
bool al_open_directory(ALLEGRO_FS_ENTRY* e);
ALLEGRO_FS_ENTRY* al_read_directory(ALLEGRO_FS_ENTRY* e);
bool al_close_directory(ALLEGRO_FS_ENTRY* e);

// Threads
ALLEGRO_THREAD* al_create_thread(void* (*proc)(ALLEGRO_THREAD*, void*), void* arg);
void al_start_thread(ALLEGRO_THREAD* t);
void al_join_thread(ALLEGRO_THREAD* t, void** ret);
void al_destroy_thread(ALLEGRO_THREAD* t);
ALLEGRO_MUTEX* al_create_mutex();
void al_destroy_mutex(ALLEGRO_MUTEX* m);
void al_lock_mutex(ALLEGRO_MUTEX* m);
void al_unlock_mutex(ALLEGRO_MUTEX* m);
ALLEGRO_COND* al_create_cond();
void al_destroy_cond(ALLEGRO_COND* c);
void al_wait_cond(ALLEGRO_COND* c, ALLEGRO_MUTEX* m);
void al_signal_cond(ALLEGRO_COND* c);
void al_broadcast_cond(ALLEGRO_COND* c);

// Shaders
ALLEGRO_SHADER* al_create_shader(ALLEGRO_SHADER_PLATFORM platform);
void al_destroy_shader(ALLEGRO_SHADER* s);
bool al_attach_shader_source(ALLEGRO_SHADER* s, ALLEGRO_SHADER_TYPE type, const char* source);
bool al_build_shader(ALLEGRO_SHADER* s);
const char* al_get_shader_log(ALLEGRO_SHADER* s);
ALLEGRO_SHADER_PLATFORM al_get_shader_platform(ALLEGRO_SHADER* s);
bool al_use_shader(ALLEGRO_SHADER* s);
const char* al_get_default_shader_source(ALLEGRO_SHADER_PLATFORM platform, ALLEGRO_SHADER_TYPE type);
bool al_set_shader_float_vector(const char* name, int components, const float* values, int elements);

// Native dialogs
int al_show_native_message_box(ALLEGRO_DISPLAY* d, const char* title, const char* heading, const char* text, const char* buttons, int flags);
ALLEGRO_TEXTLOG* al_open_native_text_log(const char* title, int flags);
void al_close_native_text_log(ALLEGRO_TEXTLOG* log);
void al_append_native_text_log(ALLEGRO_TEXTLOG* log, const char* fmt, ...);

// Audio
ALLEGRO_MIXER* al_get_default_mixer();
bool al_reserve_samples(int n);
unsigned al_get_channel_count(ALLEGRO_CHANNEL_CONF conf);
size_t al_get_audio_depth_size(ALLEGRO_AUDIO_DEPTH depth);
ALLEGRO_AUDIO_STREAM* al_load_audio_stream(const char* path, size_t buffers, unsigned samples);
ALLEGRO_AUDIO_STREAM* al_load_audio_stream_f(ALLEGRO_FILE* f, const char* ident, size_t buffers, unsigned samples);
void al_destroy_audio_stream(ALLEGRO_AUDIO_STREAM* s);
bool al_attach_audio_stream_to_mixer(ALLEGRO_AUDIO_STREAM* s, ALLEGRO_MIXER* m);
unsigned al_get_audio_stream_frequency(const ALLEGRO_AUDIO_STREAM* s);
double al_get_audio_stream_length_secs(ALLEGRO_AUDIO_STREAM* s);
double al_get_audio_stream_position_secs(ALLEGRO_AUDIO_STREAM* s);
bool al_get_audio_stream_playing(const ALLEGRO_AUDIO_STREAM* s);
bool al_seek_audio_stream_secs(ALLEGRO_AUDIO_STREAM* s, double t);
bool al_set_audio_stream_gain(ALLEGRO_AUDIO_STREAM* s, float gain);
bool al_set_audio_stream_loop_secs(ALLEGRO_AUDIO_STREAM* s, double start, double end);
bool al_set_audio_stream_pan(ALLEGRO_AUDIO_STREAM* s, float pan);
bool al_set_audio_stream_playing(ALLEGRO_AUDIO_STREAM* s, bool playing);
bool al_set_audio_stream_playmode(ALLEGRO_AUDIO_STREAM* s, ALLEGRO_PLAYMODE mode);
bool al_set_audio_stream_speed(ALLEGRO_AUDIO_STREAM* s, float speed);
ALLEGRO_SAMPLE* al_create_sample(void* buf, unsigned samples, unsigned freq, ALLEGRO_AUDIO_DEPTH depth, ALLEGRO_CHANNEL_CONF conf, bool free_buf);
void al_destroy_sample(ALLEGRO_SAMPLE* s);
ALLEGRO_SAMPLE* al_load_sample(const char* path);
ALLEGRO_SAMPLE* al_load_sample_f(ALLEGRO_FILE* f, const char* ident);
bool al_save_sample(const char* path, ALLEGRO_SAMPLE* s);
ALLEGRO_CHANNEL_CONF al_get_sample_channels(const ALLEGRO_SAMPLE* s);
void* al_get_sample_data(const ALLEGRO_SAMPLE* s);
ALLEGRO_AUDIO_DEPTH al_get_sample_depth(const ALLEGRO_SAMPLE* s);
unsigned al_get_sample_frequency(const ALLEGRO_SAMPLE* s);
unsigned al_get_sample_length(const ALLEGRO_SAMPLE* s);
ALLEGRO_SAMPLE_INSTANCE* al_create_sample_instance(ALLEGRO_SAMPLE* s);
void al_destroy_sample_instance(ALLEGRO_SAMPLE_INSTANCE* i);
bool al_attach_sample_instance_to_mixer(ALLEGRO_SAMPLE_INSTANCE* i, ALLEGRO_MIXER* m);
bool al_play_sample_instance(ALLEGRO_SAMPLE_INSTANCE* i);
bool al_stop_sample_instance(ALLEGRO_SAMPLE_INSTANCE* i);
bool al_get_sample_instance_playing(const ALLEGRO_SAMPLE_INSTANCE* i);
unsigned al_get_sample_instance_position(const ALLEGRO_SAMPLE_INSTANCE* i);
bool al_set_sample_instance_gain(ALLEGRO_SAMPLE_INSTANCE* i, float gain);
bool al_set_sample_instance_pan(ALLEGRO_SAMPLE_INSTANCE* i, float pan);
bool al_set_sample_instance_playmode(ALLEGRO_SAMPLE_INSTANCE* i, ALLEGRO_PLAYMODE mode);
bool al_set_sample_instance_position(ALLEGRO_SAMPLE_INSTANCE* i, unsigned pos);
bool al_set_sample_instance_speed(ALLEGRO_SAMPLE_INSTANCE* i, float speed);

#endif

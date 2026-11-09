// The Allegro-5-named calls the engine source makes, implemented from scratch on
// the standard library and zcport. Files, config, threads, colors and memory
// bitmaps are real. Drawing to the screen, audio and joysticks are placeholders
// until Phase 2 of UPSTREAM_INTEGRATION_PLAN.md connects them to Vulkan, the
// mixer and SDL3.
#include <allegro5/allegro.h>
#include <a5alleg.h>
#include "zcport/core.h"
#include "zcport/platform.h"
#include <process.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ---- system ----------------------------------------------------------------
static std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();

bool al_init()
{
	// The engine asks for a large window over a 640x480 screen; zcport's presenter
	// scales that screen to the window, so the mode is just the screen size.
	zcport_gfx_fit_virtual = 1;
	zcport_config_value_notes = 1;
	// The offscreen check runs with no platform at all: no window, no focus change.
	if (getenv("ZC_ENGINE_OFFSCREEN"))
		zcport_set_platform(nullptr);
	return true;
}
bool al_init_font_addon() { return true; }
bool al_init_image_addon() { return true; }
bool al_init_primitives_addon() { return true; }
bool al_init_acodec_addon() { return true; }
void al_register_trace_handler(void (*)(const char*)) {}
double al_get_time() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count(); }
void* al_malloc(size_t n) { return malloc(n); }
void al_free(void* p) { free(p); }

// ---- colors ----------------------------------------------------------------
ALLEGRO_COLOR al_map_rgba_f(float r, float g, float b, float a) { return ALLEGRO_COLOR{r, g, b, a}; }
ALLEGRO_COLOR al_map_rgb_f(float r, float g, float b) { return ALLEGRO_COLOR{r, g, b, 1.0f}; }
ALLEGRO_COLOR al_map_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a) { return ALLEGRO_COLOR{r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f}; }
ALLEGRO_COLOR al_map_rgb(unsigned char r, unsigned char g, unsigned char b) { return al_map_rgba(r, g, b, 255); }
ALLEGRO_COLOR al_premul_rgba_f(float r, float g, float b, float a) { return ALLEGRO_COLOR{r * a, g * a, b * a, a}; }
ALLEGRO_COLOR al_premul_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a) { return al_premul_rgba_f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f); }

static unsigned char to_byte(float f) { return (unsigned char)(f <= 0 ? 0 : f >= 1 ? 255 : f * 255.0f + 0.5f); }
void al_unmap_rgba(ALLEGRO_COLOR c, unsigned char* r, unsigned char* g, unsigned char* b, unsigned char* a) { *r = to_byte(c.r); *g = to_byte(c.g); *b = to_byte(c.b); *a = to_byte(c.a); }
void al_unmap_rgb(ALLEGRO_COLOR c, unsigned char* r, unsigned char* g, unsigned char* b) { unsigned char a; al_unmap_rgba(c, r, g, b, &a); }

// ---- bitmaps ---------------------------------------------------------------
// Pixels are 32-bit with red in the low byte (ABGR_8888_LE).
struct ALLEGRO_BITMAP
{
	int w, h, flags;
	int clip_x, clip_y, clip_w, clip_h;
	std::vector<uint32_t> px;
	ALLEGRO_LOCKED_REGION region;
};

static int g_new_flags = 0;
static ALLEGRO_BITMAP* g_target = nullptr;
static int g_blend_op = ALLEGRO_ADD, g_blend_src = ALLEGRO_ONE, g_blend_dst = ALLEGRO_INVERSE_ALPHA;

ALLEGRO_BITMAP* al_create_bitmap(int w, int h)
{
	auto* b = new ALLEGRO_BITMAP();
	b->w = w;
	b->h = h;
	b->flags = g_new_flags;
	b->clip_x = b->clip_y = 0;
	b->clip_w = w;
	b->clip_h = h;
	b->px.assign((size_t)(w > 0 ? w : 0) * (h > 0 ? h : 0), 0);
	return b;
}
void al_destroy_bitmap(ALLEGRO_BITMAP* b) { if (g_target == b) g_target = nullptr; delete b; }
int al_get_bitmap_width(ALLEGRO_BITMAP* b) { return b ? b->w : 0; }
int al_get_bitmap_height(ALLEGRO_BITMAP* b) { return b ? b->h : 0; }
int al_get_bitmap_flags(ALLEGRO_BITMAP* b) { return b ? b->flags : 0; }
int al_get_bitmap_format(ALLEGRO_BITMAP*) { return ALLEGRO_PIXEL_FORMAT_ABGR_8888_LE; }
int al_get_new_bitmap_flags() { return g_new_flags; }
void al_set_new_bitmap_flags(int flags) { g_new_flags = flags; }
ALLEGRO_LOCKED_REGION* al_lock_bitmap(ALLEGRO_BITMAP* b, int, int)
{
	if (!b)
		return nullptr;
	b->region.data = b->px.data();
	b->region.format = ALLEGRO_PIXEL_FORMAT_ABGR_8888_LE;
	b->region.pitch = b->w * 4;
	b->region.pixel_size = 4;
	return &b->region;
}
void al_unlock_bitmap(ALLEGRO_BITMAP*) {}
void al_put_pixel(int x, int y, ALLEGRO_COLOR c)
{
	if (!g_target || x < 0 || y < 0 || x >= g_target->w || y >= g_target->h)
		return;
	unsigned char r, g, b, a;
	al_unmap_rgba(c, &r, &g, &b, &a);
	g_target->px[(size_t)y * g_target->w + x] = (uint32_t)r | (g << 8) | (b << 16) | ((uint32_t)a << 24);
}
void al_set_target_bitmap(ALLEGRO_BITMAP* b) { g_target = b; }
ALLEGRO_BITMAP* al_get_target_bitmap() { return g_target; }
void al_clear_to_color(ALLEGRO_COLOR c)
{
	if (!g_target)
		return;
	unsigned char r, g, b, a;
	al_unmap_rgba(c, &r, &g, &b, &a);
	std::fill(g_target->px.begin(), g_target->px.end(), (uint32_t)r | (g << 8) | (b << 16) | ((uint32_t)a << 24));
}
void al_set_blender(int op, int src, int dst) { g_blend_op = op; g_blend_src = src; g_blend_dst = dst; }
void al_set_clipping_rectangle(int x, int y, int w, int h)
{
	if (!g_target)
		return;
	g_target->clip_x = x;
	g_target->clip_y = y;
	g_target->clip_w = w;
	g_target->clip_h = h;
}
void al_get_clipping_rectangle(int* x, int* y, int* w, int* h)
{
	*x = g_target ? g_target->clip_x : 0;
	*y = g_target ? g_target->clip_y : 0;
	*w = g_target ? g_target->clip_w : 0;
	*h = g_target ? g_target->clip_h : 0;
}
void al_reset_clipping_rectangle()
{
	if (g_target)
		al_set_clipping_rectangle(0, 0, g_target->w, g_target->h);
}
void al_store_state(ALLEGRO_STATE* s, int flags)
{
	s->flags = flags;
	s->target = g_target;
	s->new_bitmap_flags = g_new_flags;
	s->blend_op = g_blend_op;
	s->blend_src = g_blend_src;
	s->blend_dst = g_blend_dst;
}
void al_restore_state(const ALLEGRO_STATE* s)
{
	if (s->flags & ALLEGRO_STATE_TARGET_BITMAP)
		g_target = s->target;
	if (s->flags & ALLEGRO_STATE_NEW_BITMAP_PARAMETERS)
		g_new_flags = s->new_bitmap_flags;
	if (s->flags & ALLEGRO_STATE_BLENDER)
	{
		g_blend_op = s->blend_op;
		g_blend_src = s->blend_src;
		g_blend_dst = s->blend_dst;
	}
}
bool al_save_bitmap(const char*, ALLEGRO_BITMAP*) { return false; }

// Software drawing into the target bitmap: the layers the engine composites each
// frame, with nearest-neighbour scaling and the current blender.
static int blend_factor(int mode, int src_alpha)
{
	switch (mode)
	{
		case ALLEGRO_ZERO: return 0;
		case ALLEGRO_ONE: return 255;
		case ALLEGRO_ALPHA: return src_alpha;
		default: return 255 - src_alpha; // ALLEGRO_INVERSE_ALPHA
	}
}
static inline uint32_t blend_pixel(uint32_t dst, int sr, int sg, int sb, int sa)
{
	int fs = blend_factor(g_blend_src, sa), fd = blend_factor(g_blend_dst, sa);
	int dr = dst & 255, dg = (dst >> 8) & 255, db = (dst >> 16) & 255, da = dst >> 24;
	int r = std::min(255, (sr * fs + dr * fd) / 255);
	int g = std::min(255, (sg * fs + dg * fd) / 255);
	int b = std::min(255, (sb * fs + db * fd) / 255);
	int a = std::min(255, (sa * fs + da * fd) / 255);
	return (uint32_t)r | (g << 8) | (b << 16) | ((uint32_t)a << 24);
}
static void clip_span(int& x0, int& y0, int& x1, int& y1)
{
	x0 = std::max({x0, g_target->clip_x, 0});
	y0 = std::max({y0, g_target->clip_y, 0});
	x1 = std::min({x1, g_target->clip_x + g_target->clip_w, g_target->w});
	y1 = std::min({y1, g_target->clip_y + g_target->clip_h, g_target->h});
}
static void draw_region(ALLEGRO_BITMAP* b, const ALLEGRO_COLOR* tint, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh)
{
	if (!g_target || !b || b == g_target || dw <= 0 || dh <= 0 || b->px.empty())
		return;
	int x0 = (int)floorf(dx), y0 = (int)floorf(dy), x1 = (int)ceilf(dx + dw), y1 = (int)ceilf(dy + dh);
	clip_span(x0, y0, x1, y1);
	const float u_step = sw / dw, v_step = sh / dh;
	const bool plain = !tint && g_blend_src == ALLEGRO_ONE && g_blend_dst == ALLEGRO_INVERSE_ALPHA;
	for (int y = y0; y < y1; y++)
	{
		int v = (int)(sy + (y + 0.5f - dy) * v_step);
		if (v < 0 || v >= b->h)
			continue;
		const uint32_t* src = &b->px[(size_t)v * b->w];
		uint32_t* out = &g_target->px[(size_t)y * g_target->w];
		for (int x = x0; x < x1; x++)
		{
			int u = (int)(sx + (x + 0.5f - dx) * u_step);
			if (u < 0 || u >= b->w)
				continue;
			uint32_t p = src[u];
			if (plain && (p >> 24) == 255)
			{
				out[x] = p;
				continue;
			}
			int r = p & 255, g = (p >> 8) & 255, bl = (p >> 16) & 255, a = p >> 24;
			if (tint)
			{
				r = (int)(r * tint->r);
				g = (int)(g * tint->g);
				bl = (int)(bl * tint->b);
				a = (int)(a * tint->a);
			}
			out[x] = blend_pixel(out[x], r, g, bl, a);
		}
	}
}
void al_draw_bitmap(ALLEGRO_BITMAP* b, float dx, float dy, int)
{
	if (b)
		draw_region(b, nullptr, 0, 0, (float)b->w, (float)b->h, dx, dy, (float)b->w, (float)b->h);
}
void al_draw_scaled_bitmap(ALLEGRO_BITMAP* b, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, int)
{
	draw_region(b, nullptr, sx, sy, sw, sh, dx, dy, dw, dh);
}
void al_draw_tinted_scaled_bitmap(ALLEGRO_BITMAP* b, ALLEGRO_COLOR tint, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, int)
{
	draw_region(b, &tint, sx, sy, sw, sh, dx, dy, dw, dh);
}
void al_draw_filled_rectangle(float x1, float y1, float x2, float y2, ALLEGRO_COLOR c)
{
	if (!g_target)
		return;
	int x0 = (int)floorf(std::min(x1, x2)), y0 = (int)floorf(std::min(y1, y2));
	int xe = (int)ceilf(std::max(x1, x2)), ye = (int)ceilf(std::max(y1, y2));
	clip_span(x0, y0, xe, ye);
	unsigned char r, g, b, a;
	al_unmap_rgba(c, &r, &g, &b, &a);
	for (int y = y0; y < ye; y++)
	{
		uint32_t* out = &g_target->px[(size_t)y * g_target->w];
		for (int x = x0; x < xe; x++)
			out[x] = blend_pixel(out[x], r, g, b, a);
	}
}
void al_draw_line(float x1, float y1, float x2, float y2, ALLEGRO_COLOR c, float)
{
	// Only axis-aligned hairlines appear outside the debug overlays.
	al_draw_filled_rectangle(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2) + 1, std::max(y1, y2) + 1, c);
}
void al_draw_rectangle(float x1, float y1, float x2, float y2, ALLEGRO_COLOR c, float t)
{
	al_draw_line(x1, y1, x2, y1, c, t);
	al_draw_line(x1, y2, x2, y2, c, t);
	al_draw_line(x1, y1, x1, y2, c, t);
	al_draw_line(x2, y1, x2, y2, c, t);
}
// Debug overlay shapes.
void al_draw_circle(float, float, float, ALLEGRO_COLOR, float) {}
void al_draw_filled_circle(float, float, float, ALLEGRO_COLOR) {}
void al_draw_arc(float, float, float, float, float, ALLEGRO_COLOR, float) {}

// ---- fonts -----------------------------------------------------------------
struct ALLEGRO_FONT { int line_height; };
void al_draw_text(const ALLEGRO_FONT*, ALLEGRO_COLOR c, float dx, float dy, int, const char* text)
{
	if (!g_target || !text || !*text)
		return;
	BITMAP* glyphs = create_bitmap_ex(8, text_length(font, text), text_height(font));
	if (!glyphs)
		return;
	clear_to_color(glyphs, 0);
	textout_ex(glyphs, font, text, 0, 0, 1, -1);
	unsigned char r, g, b, a;
	al_unmap_rgba(c, &r, &g, &b, &a);
	int x0 = (int)dx, y0 = (int)dy, x1 = x0 + glyphs->w, y1 = y0 + glyphs->h;
	clip_span(x0, y0, x1, y1);
	for (int y = y0; y < y1; y++)
	{
		uint32_t* out = &g_target->px[(size_t)y * g_target->w];
		for (int x = x0; x < x1; x++)
			if (glyphs->line[y - (int)dy][x - (int)dx])
				out[x] = blend_pixel(out[x], r, g, b, a);
	}
	destroy_bitmap(glyphs);
}
void al_destroy_font(ALLEGRO_FONT* f) { delete f; }
int al_get_font_line_height(const ALLEGRO_FONT*) { return text_height(font); }
int al_get_text_width(const ALLEGRO_FONT*, const char* text) { return text ? text_length(font, text) : 0; }
ALLEGRO_FONT* al_grab_font_from_bitmap(ALLEGRO_BITMAP* b, int, const int*) { return new ALLEGRO_FONT{b ? b->h : 8}; }

// ---- display ---------------------------------------------------------------
struct ALLEGRO_DISPLAY
{
	int w, h, x, y;
	ALLEGRO_BITMAP* backbuffer;
};
struct ALLEGRO_EVENT_SOURCE { int unused; };
struct ALLEGRO_EVENT_QUEUE { int unused; };
struct ALLEGRO_MOUSE_CURSOR { int unused; };

static ALLEGRO_EVENT_SOURCE g_source;
static int g_display_flags = 0;

ALLEGRO_DISPLAY* al_create_display(int w, int h) { return new ALLEGRO_DISPLAY{w, h, 0, 0, al_create_bitmap(w, h)}; }
void al_destroy_display(ALLEGRO_DISPLAY* d) { if (d) { al_destroy_bitmap(d->backbuffer); delete d; } }
static ALLEGRO_DISPLAY* main_display()
{
	static ALLEGRO_DISPLAY* d = al_create_display(640, 480);
	if (gfx_driver && (d->w != gfx_driver->w || d->h != gfx_driver->h))
	{
		d->w = gfx_driver->w;
		d->h = gfx_driver->h;
		al_destroy_bitmap(d->backbuffer);
		d->backbuffer = al_create_bitmap(d->w, d->h);
	}
	return d;
}
// Hands the finished frame to zcport's window, which shows it through Vulkan.
void al_flip_display()
{
	ALLEGRO_BITMAP* b = main_display()->backbuffer;
	static BITMAP* frame = nullptr;
	if (!frame || frame->w != b->w || frame->h != b->h)
	{
		if (frame)
			destroy_bitmap(frame);
		frame = create_bitmap_ex(32, b->w, b->h);
	}
	for (int y = 0; y < b->h; y++)
	{
		const uint32_t* in = &b->px[(size_t)y * b->w];
		uint32_t* out = (uint32_t*)frame->line[y];
		for (int x = 0; x < b->w; x++)
			out[x] = ((in[x] & 0xFF) << 16) | (in[x] & 0xFF00) | ((in[x] >> 16) & 0xFF);
	}
	zcport_present_bitmap(frame);

	// Offscreen check (ZC_ENGINE_OFFSCREEN="file.bmp,frame"): saves one frame and exits.
	static const char* shot = getenv("ZC_ENGINE_OFFSCREEN");
	static int frames = 0;
	if (shot && ++frames >= atoi(strchr(shot, ',') + 1))
	{
		std::string path(shot, strchr(shot, ',') - shot);
		save_bitmap(path.c_str(), frame, _current_palette);
		fflush(nullptr);
		_exit(0);
	}
}
int al_get_display_width(ALLEGRO_DISPLAY* d) { return d ? d->w : 0; }
int al_get_display_height(ALLEGRO_DISPLAY* d) { return d ? d->h : 0; }
bool al_resize_display(ALLEGRO_DISPLAY* d, int w, int h) { if (!d) return false; d->w = w; d->h = h; return true; }
bool al_acknowledge_resize(ALLEGRO_DISPLAY*) { return true; }
ALLEGRO_EVENT_SOURCE* al_get_display_event_source(ALLEGRO_DISPLAY*) { return &g_source; }
void al_set_window_position(ALLEGRO_DISPLAY* d, int x, int y) { if (d) { d->x = x; d->y = y; } }
void al_get_window_position(ALLEGRO_DISPLAY* d, int* x, int* y) { *x = d ? d->x : 0; *y = d ? d->y : 0; }
void al_set_window_title(ALLEGRO_DISPLAY*, const char*) {}
void al_set_display_icon(ALLEGRO_DISPLAY*, ALLEGRO_BITMAP*) {}
void al_set_new_display_flags(int) {}
void al_set_new_display_option(int, int, int) {}
bool al_get_monitor_info(int, ALLEGRO_MONITOR_INFO* i) { i->x1 = 0; i->y1 = 0; i->x2 = 1920; i->y2 = 1080; return true; }
int al_get_monitor_dpi(int) { return 96; }
bool al_clipboard_has_text(ALLEGRO_DISPLAY*) { return false; }
char* al_get_clipboard_text(ALLEGRO_DISPLAY*) { return nullptr; }
bool al_set_clipboard_text(ALLEGRO_DISPLAY*, const char*) { return false; }
void al_set_target_backbuffer(ALLEGRO_DISPLAY* d) { g_target = d ? d->backbuffer : nullptr; }
ALLEGRO_BITMAP* al_get_backbuffer(ALLEGRO_DISPLAY* d) { return d ? d->backbuffer : nullptr; }

ALLEGRO_MOUSE_CURSOR* al_create_mouse_cursor(ALLEGRO_BITMAP*, int, int) { return new ALLEGRO_MOUSE_CURSOR(); }
void al_destroy_mouse_cursor(ALLEGRO_MOUSE_CURSOR* c) { delete c; }
bool al_set_mouse_cursor(ALLEGRO_DISPLAY*, ALLEGRO_MOUSE_CURSOR*) { return true; }
bool al_set_system_mouse_cursor(ALLEGRO_DISPLAY*, int) { return true; }
bool al_show_mouse_cursor(ALLEGRO_DISPLAY*) { return true; }
bool al_is_mouse_installed() { return false; }
ALLEGRO_EVENT_SOURCE* al_get_mouse_event_source() { return &g_source; }
ALLEGRO_EVENT_SOURCE* al_get_keyboard_event_source() { return &g_source; }
ALLEGRO_EVENT_QUEUE* al_create_event_queue() { return new ALLEGRO_EVENT_QUEUE(); }
void al_destroy_event_queue(ALLEGRO_EVENT_QUEUE* q) { delete q; }
void al_register_event_source(ALLEGRO_EVENT_QUEUE*, ALLEGRO_EVENT_SOURCE*) {}
bool al_get_next_event(ALLEGRO_EVENT_QUEUE*, ALLEGRO_EVENT*) { return false; }

// Joysticks: what the engine asks of the Allegro-5-named calls is identity and
// counts; the live state it reads comes from zcport's joy[] array.
struct ALLEGRO_JOYSTICK { int index; };
int al_get_num_joysticks() { return num_joysticks; }
ALLEGRO_JOYSTICK* al_get_joystick(int i)
{
	static ALLEGRO_JOYSTICK sticks[MAX_JOYSTICKS];
	if (i < 0 || i >= num_joysticks || i >= MAX_JOYSTICKS)
		return nullptr;
	sticks[i].index = i;
	return &sticks[i];
}
bool al_get_joystick_active(ALLEGRO_JOYSTICK* j) { return j && j->index < num_joysticks; }
ALLEGRO_JOYSTICK_GUID al_get_joystick_guid(ALLEGRO_JOYSTICK*) { return ALLEGRO_JOYSTICK_GUID{}; }
const char* al_get_joystick_name(ALLEGRO_JOYSTICK* j)
{
	static char names[MAX_JOYSTICKS][16];
	if (!j)
		return "";
	snprintf(names[j->index], sizeof(names[0]), "Joystick %d", j->index + 1);
	return names[j->index];
}
int al_get_joystick_num_sticks(ALLEGRO_JOYSTICK* j) { return j ? joy[j->index].num_sticks : 0; }
int al_get_joystick_num_buttons(ALLEGRO_JOYSTICK* j) { return j ? joy[j->index].num_buttons : 0; }
ALLEGRO_JOYSTICK_TYPE al_get_joystick_type(ALLEGRO_JOYSTICK* j) { return j && joy[j->index].num_buttons >= 10 ? ALLEGRO_JOYSTICK_TYPE_GAMEPAD : ALLEGRO_JOYSTICK_TYPE_UNKNOWN; }
bool al_set_joystick_mappings(const char*) { return true; }
extern "C" int _al_sdl_joystick_controller_type(ALLEGRO_JOYSTICK*) { return -1; }
extern "C" int _al_sdl_joystick_mapping_uses_labels(ALLEGRO_JOYSTICK*) { return -1; }

// ---- config ----------------------------------------------------------------
struct ALLEGRO_CONFIG_ENTRY
{
	std::string key, value;
	bool comment;
	ALLEGRO_CONFIG_ENTRY* next;
};
struct ALLEGRO_CONFIG_SECTION
{
	std::string name;
	ALLEGRO_CONFIG_ENTRY* first, *last;
	ALLEGRO_CONFIG_SECTION* next;
};
struct ALLEGRO_CONFIG
{
	ALLEGRO_CONFIG_SECTION* first = nullptr, *last = nullptr;
};

static ALLEGRO_CONFIG_SECTION* find_section(const ALLEGRO_CONFIG* c, const char* name)
{
	std::string n = name ? name : "";
	for (auto* s = c->first; s; s = s->next)
		if (s->name == n)
			return s;
	return nullptr;
}
static ALLEGRO_CONFIG_SECTION* need_section(ALLEGRO_CONFIG* c, const char* name)
{
	if (auto* s = find_section(c, name))
		return s;
	auto* s = new ALLEGRO_CONFIG_SECTION{name ? name : "", nullptr, nullptr, nullptr};
	(c->last ? c->last->next : c->first) = s;
	c->last = s;
	return s;
}
static void append_entry(ALLEGRO_CONFIG_SECTION* s, const std::string& k, const std::string& v, bool comment)
{
	auto* e = new ALLEGRO_CONFIG_ENTRY{k, v, comment, nullptr};
	(s->last ? s->last->next : s->first) = e;
	s->last = e;
}
ALLEGRO_CONFIG* al_create_config() { return new ALLEGRO_CONFIG(); }
void al_destroy_config(ALLEGRO_CONFIG* c)
{
	if (!c)
		return;
	for (auto* s = c->first; s;)
	{
		for (auto* e = s->first; e;)
		{
			auto* n = e->next;
			delete e;
			e = n;
		}
		auto* n = s->next;
		delete s;
		s = n;
	}
	delete c;
}
static std::string trimmed(std::string s)
{
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos)
		return "";
	return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
ALLEGRO_CONFIG* al_load_config_file(const char* path)
{
	std::ifstream in(path);
	if (!in)
		return nullptr;
	auto* c = al_create_config();
	auto* s = need_section(c, "");
	std::string line;
	while (std::getline(in, line))
	{
		std::string t = trimmed(line);
		if (t.empty())
			continue;
		if (t[0] == '#' || t[0] == ';')
			append_entry(s, "", t, true);
		else if (t[0] == '[' && t.back() == ']')
			s = need_section(c, t.substr(1, t.size() - 2).c_str());
		else
		{
			size_t eq = t.find('=');
			if (eq != std::string::npos)
				append_entry(s, trimmed(t.substr(0, eq)), trimmed(t.substr(eq + 1)), false);
		}
	}
	return c;
}
bool al_save_config_file(const char* path, const ALLEGRO_CONFIG* c)
{
	std::ofstream out(path);
	if (!out)
		return false;
	for (auto* s = c->first; s; s = s->next)
	{
		if (!s->name.empty())
			out << "[" << s->name << "]\n";
		for (auto* e = s->first; e; e = e->next)
		{
			if (e->comment)
				out << e->value << "\n";
			else
				out << e->key << " = " << e->value << "\n";
		}
		out << "\n";
	}
	return true;
}
const char* al_get_config_value(const ALLEGRO_CONFIG* c, const char* section, const char* key)
{
	auto* s = find_section(c, section);
	if (!s)
		return nullptr;
	for (auto* e = s->first; e; e = e->next)
		if (!e->comment && e->key == key)
			return e->value.c_str();
	return nullptr;
}
void al_set_config_value(ALLEGRO_CONFIG* c, const char* section, const char* key, const char* value)
{
	auto* s = need_section(c, section);
	for (auto* e = s->first; e; e = e->next)
	{
		if (!e->comment && e->key == key)
		{
			e->value = value;
			return;
		}
	}
	append_entry(s, key, value, false);
}
void al_add_config_comment(ALLEGRO_CONFIG* c, const char* section, const char* comment) { append_entry(need_section(c, section), "", std::string("# ") + comment, true); }
void al_add_config_section(ALLEGRO_CONFIG* c, const char* section) { need_section(c, section); }
bool al_remove_config_key(ALLEGRO_CONFIG* c, const char* section, const char* key)
{
	auto* s = find_section(c, section);
	if (!s)
		return false;
	ALLEGRO_CONFIG_ENTRY* prev = nullptr;
	for (auto* e = s->first; e; prev = e, e = e->next)
	{
		if (!e->comment && e->key == key)
		{
			(prev ? prev->next : s->first) = e->next;
			if (s->last == e)
				s->last = prev;
			delete e;
			return true;
		}
	}
	return false;
}
bool al_remove_config_section(ALLEGRO_CONFIG* c, const char* section)
{
	std::string n = section ? section : "";
	ALLEGRO_CONFIG_SECTION* prev = nullptr;
	for (auto* s = c->first; s; prev = s, s = s->next)
	{
		if (s->name == n)
		{
			(prev ? prev->next : c->first) = s->next;
			if (c->last == s)
				c->last = prev;
			for (auto* e = s->first; e;)
			{
				auto* next = e->next;
				delete e;
				e = next;
			}
			delete s;
			return true;
		}
	}
	return false;
}
const char* al_get_first_config_section(const ALLEGRO_CONFIG* c, ALLEGRO_CONFIG_SECTION** it)
{
	*it = c->first;
	return *it ? (*it)->name.c_str() : nullptr;
}
const char* al_get_next_config_section(ALLEGRO_CONFIG_SECTION** it)
{
	if (*it)
		*it = (*it)->next;
	return *it ? (*it)->name.c_str() : nullptr;
}
static ALLEGRO_CONFIG_ENTRY* skip_comments(ALLEGRO_CONFIG_ENTRY* e)
{
	while (e && e->comment)
		e = e->next;
	return e;
}
const char* al_get_first_config_entry(const ALLEGRO_CONFIG* c, const char* section, ALLEGRO_CONFIG_ENTRY** it)
{
	auto* s = find_section(c, section);
	*it = s ? skip_comments(s->first) : nullptr;
	return *it ? (*it)->key.c_str() : nullptr;
}
const char* al_get_next_config_entry(ALLEGRO_CONFIG_ENTRY** it)
{
	if (*it)
		*it = skip_comments((*it)->next);
	return *it ? (*it)->key.c_str() : nullptr;
}
void al_merge_config_into(ALLEGRO_CONFIG* master, const ALLEGRO_CONFIG* add)
{
	for (auto* s = add->first; s; s = s->next)
		for (auto* e = s->first; e; e = e->next)
			if (!e->comment)
				al_set_config_value(master, s->name.c_str(), e->key.c_str(), e->value.c_str());
}
ALLEGRO_CONFIG* al_get_system_config()
{
	static ALLEGRO_CONFIG* c = al_create_config();
	return c;
}

// ---- files -----------------------------------------------------------------
struct ALLEGRO_FILE
{
	FILE* fp;
	unsigned char* mem;
	int64_t size, pos;
};
ALLEGRO_FILE* al_fopen(const char* path, const char* mode)
{
	FILE* fp = fopen(path, mode);
	return fp ? new ALLEGRO_FILE{fp, nullptr, 0, 0} : nullptr;
}
ALLEGRO_FILE* al_open_memfile(void* mem, int64_t size, const char*) { return new ALLEGRO_FILE{nullptr, (unsigned char*)mem, size, 0}; }
bool al_fclose(ALLEGRO_FILE* f)
{
	if (!f)
		return true;
	bool ok = !f->fp || fclose(f->fp) == 0;
	delete f;
	return ok;
}
bool al_fflush(ALLEGRO_FILE* f) { return !f->fp || fflush(f->fp) == 0; }
int al_fputs(ALLEGRO_FILE* f, const char* s) { return f->fp ? fputs(s, f->fp) : -1; }
size_t al_fread(ALLEGRO_FILE* f, void* p, size_t n)
{
	if (f->fp)
		return fread(p, 1, n, f->fp);
	size_t got = (size_t)std::min<int64_t>((int64_t)n, f->size - f->pos);
	memcpy(p, f->mem + f->pos, got);
	f->pos += got;
	return got;
}
size_t al_fwrite(ALLEGRO_FILE* f, const void* p, size_t n)
{
	if (f->fp)
		return fwrite(p, 1, n, f->fp);
	size_t put = (size_t)std::min<int64_t>((int64_t)n, f->size - f->pos);
	memcpy(f->mem + f->pos, p, put);
	f->pos += put;
	return put;
}
int64_t al_fsize(ALLEGRO_FILE* f)
{
	if (!f->fp)
		return f->size;
	int64_t cur = _ftelli64(f->fp);
	_fseeki64(f->fp, 0, SEEK_END);
	int64_t end = _ftelli64(f->fp);
	_fseeki64(f->fp, cur, SEEK_SET);
	return end;
}
bool al_filename_exists(const char* path) { std::error_code ec; return fs::exists(path, ec); }
bool al_make_directory(const char* path) { std::error_code ec; fs::create_directories(path, ec); return !ec; }

struct ALLEGRO_FS_ENTRY
{
	std::string path;
	fs::directory_iterator it;
	bool open = false;
};
ALLEGRO_FS_ENTRY* al_create_fs_entry(const char* path) { auto* e = new ALLEGRO_FS_ENTRY(); e->path = *path ? path : "."; return e; }
void al_destroy_fs_entry(ALLEGRO_FS_ENTRY* e) { delete e; }
uint32_t al_get_fs_entry_mode(ALLEGRO_FS_ENTRY* e) { std::error_code ec; return fs::is_directory(e->path, ec) ? ALLEGRO_FILEMODE_ISDIR : 0; }
const char* al_get_fs_entry_name(ALLEGRO_FS_ENTRY* e) { return e->path.c_str(); }
bool al_open_directory(ALLEGRO_FS_ENTRY* e)
{
	std::error_code ec;
	e->it = fs::directory_iterator(e->path, ec);
	e->open = !ec;
	return e->open;
}
ALLEGRO_FS_ENTRY* al_read_directory(ALLEGRO_FS_ENTRY* e)
{
	if (!e->open || e->it == fs::directory_iterator())
		return nullptr;
	auto* next = new ALLEGRO_FS_ENTRY();
	next->path = e->it->path().string();
	std::error_code ec;
	e->it.increment(ec);
	if (ec)
		e->it = fs::directory_iterator();
	return next;
}
bool al_close_directory(ALLEGRO_FS_ENTRY* e) { e->it = fs::directory_iterator(); e->open = false; return true; }

// ---- threads ---------------------------------------------------------------
struct ALLEGRO_THREAD
{
	void* (*proc)(ALLEGRO_THREAD*, void*);
	void* arg;
	void* result;
	std::thread t;
};
struct ALLEGRO_MUTEX { std::recursive_mutex m; };
struct ALLEGRO_COND { std::condition_variable_any c; };

ALLEGRO_THREAD* al_create_thread(void* (*proc)(ALLEGRO_THREAD*, void*), void* arg) { return new ALLEGRO_THREAD{proc, arg, nullptr, {}}; }
void al_start_thread(ALLEGRO_THREAD* t) { t->t = std::thread([t] { t->result = t->proc(t, t->arg); }); }
void al_join_thread(ALLEGRO_THREAD* t, void** ret)
{
	if (t->t.joinable())
		t->t.join();
	if (ret)
		*ret = t->result;
}
void al_destroy_thread(ALLEGRO_THREAD* t)
{
	if (t->t.joinable())
		t->t.join();
	delete t;
}
ALLEGRO_MUTEX* al_create_mutex() { return new ALLEGRO_MUTEX(); }
void al_destroy_mutex(ALLEGRO_MUTEX* m) { delete m; }
void al_lock_mutex(ALLEGRO_MUTEX* m) { m->m.lock(); }
void al_unlock_mutex(ALLEGRO_MUTEX* m) { m->m.unlock(); }
ALLEGRO_COND* al_create_cond() { return new ALLEGRO_COND(); }
void al_destroy_cond(ALLEGRO_COND* c) { delete c; }
void al_wait_cond(ALLEGRO_COND* c, ALLEGRO_MUTEX* m) { c->c.wait(m->m); }
void al_signal_cond(ALLEGRO_COND* c) { c->c.notify_one(); }
void al_broadcast_cond(ALLEGRO_COND* c) { c->c.notify_all(); }

// ---- shaders: connected to Vulkan in Phase 2 ---------------------------------
ALLEGRO_SHADER* al_create_shader(ALLEGRO_SHADER_PLATFORM) { return nullptr; }
void al_destroy_shader(ALLEGRO_SHADER*) {}
bool al_attach_shader_source(ALLEGRO_SHADER*, ALLEGRO_SHADER_TYPE, const char*) { return false; }
bool al_build_shader(ALLEGRO_SHADER*) { return false; }
const char* al_get_shader_log(ALLEGRO_SHADER*) { return "shaders are not available yet"; }
ALLEGRO_SHADER_PLATFORM al_get_shader_platform(ALLEGRO_SHADER*) { return ALLEGRO_SHADER_AUTO; }
bool al_use_shader(ALLEGRO_SHADER*) { return false; }
const char* al_get_default_shader_source(ALLEGRO_SHADER_PLATFORM, ALLEGRO_SHADER_TYPE) { return ""; }
bool al_set_shader_float_vector(const char*, int, const float*, int) { return false; }

// ---- native dialogs ----------------------------------------------------------
int al_show_native_message_box(ALLEGRO_DISPLAY*, const char* title, const char* heading, const char* text, const char*, int)
{
	fprintf(stderr, "%s: %s %s\n", title ? title : "", heading ? heading : "", text ? text : "");
	return 1;
}
struct ALLEGRO_TEXTLOG { int unused; };
ALLEGRO_TEXTLOG* al_open_native_text_log(const char*, int) { return new ALLEGRO_TEXTLOG(); }
void al_close_native_text_log(ALLEGRO_TEXTLOG* log) { delete log; }
void al_append_native_text_log(ALLEGRO_TEXTLOG*, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

// ---- bridge ------------------------------------------------------------------
ALLEGRO_DISPLAY* all_get_display() { return main_display(); }
int all_joystick_generation() { return 0; }
ALLEGRO_BITMAP* all_get_a5_bitmap(BITMAP* bp) { return al_create_bitmap(bp->w, bp->h); }

static int g_transparent_index = -1;

// Converts one of the game's software layers for compositing.
void all_render_a5_bitmap(BITMAP* bp, ALLEGRO_BITMAP* a5)
{
	if (!bp || !a5 || a5->px.empty())
		return;
	const int w = std::min(bp->w, a5->w), h = std::min(bp->h, a5->h);
	if (bitmap_color_depth(bp) == 8)
	{
		uint32_t lut[256];
		for (int i = 0; i < 256; i++)
		{
			// The engine keeps full 8-bit values in its palettes.
			const RGB& c = _current_palette[i];
			lut[i] = 0xFF000000u | (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16);
		}
		if (g_transparent_index >= 0 && g_transparent_index < 256)
			lut[g_transparent_index] = 0;
		for (int y = 0; y < h; y++)
		{
			uint32_t* out = &a5->px[(size_t)y * a5->w];
			for (int x = 0; x < w; x++)
				out[x] = lut[bp->line[y][x]];
		}
	}
	else if (bitmap_color_depth(bp) == 32)
	{
		for (int y = 0; y < h; y++)
		{
			const uint32_t* in = (const uint32_t*)bp->line[y];
			uint32_t* out = &a5->px[(size_t)y * a5->w];
			for (int x = 0; x < w; x++)
			{
				uint32_t p = in[x] & 0xFFFFFFu;
				out[x] = p == 0xFF00FFu ? 0 : 0xFF000000u | ((p >> 16) & 0xFF) | (p & 0xFF00) | ((p & 0xFF) << 16);
			}
		}
	}
}
void all_disable_threaded_display() {}
void all_process_display_events() {}
void all_set_resize_flag(bool) {}
void all_set_display_flags(int flags) { g_display_flags = flags; }
int all_get_display_flags() { return g_display_flags; }
void all_set_transparent_palette_index(int index) { g_transparent_index = index; }
// Changes whenever the palette does, so an unchanged layer is converted again.
uint32_t all_get_render_generation()
{
	uint32_t h = 2166136261u;
	for (int i = 0; i < 256; i++)
	{
		const RGB& c = _current_palette[i];
		h = (h ^ c.r) * 16777619u;
		h = (h ^ c.g) * 16777619u;
		h = (h ^ c.b) * 16777619u;
	}
	return h;
}

// What the unified engine's widget toolkit needs beyond src/port/game/jwin_port.cpp:
// its event routing, the controls 2.53 didn't have, and flat software drawing for
// the menus and self-drawing controls. Written from scratch for zcport; the dialogs
// themselves are shown by the ImGui host.
#include "gui/jwin.h"
#include "gui/dialog_runner.h"
#include "gui/scrolling_pane.h"
#include "gui/tabpanel.h"
#include "gui/text_field.h"
#include "gui/window.h"
#include "a5alleg.h"
#include "ui_dialogs_internal.h"
#include "ui_game.h"
#include "zcport/video_settings.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace zcui_dlg;

char newGuiMarker = 0;
int32_t abc_patternmatch = 0;
int32_t jwin_pal[jcMAX];

// ---- events --------------------------------------------------------------------

// d is the DIALOG before the one the event is for. The runner's root object marks
// the start of the array, and its widget list is offset by one from it.
int32_t new_gui_event(DIALOG* d, guiEvent event)
{
	DIALOG* p = d;
	for (int32_t i = 0; ; --p, ++i)
	{
		if (p->dp3 == &newGuiMarker)
		{
			p->d1 = i;
			return p->proc(MSG_GUI_EVENT, p, event);
		}
	}
}

// ---- simple controls -----------------------------------------------------------

namespace
{
	inline int answer(int kind) { return KIND_MAGIC | kind; }
}

int32_t new_text_proc(int32_t msg, DIALOG* d, int32_t)
{
	if (msg == MSG_ZCUI_ALIGN)
		return 1 + d->d1;
	return msg == MSG_ZCUI_KIND ? answer(K_TEXT) : D_O_K;
}

int32_t new_tab_proc(int32_t msg, DIALOG*, int32_t)
{
	return msg == MSG_ZCUI_KIND ? answer(K_TAB) : D_O_K;
}

// The toggle behind the toolkit's check boxes.
int32_t d_jwinbutton_proc(int32_t msg, DIALOG* d, int32_t)
{
	switch (msg)
	{
		case MSG_ZCUI_KIND:
			return answer(K_CHECK);
		case MSG_WANTFOCUS:
			return D_WANTFOCUS;
		case MSG_KEY:
		case MSG_CLICK:
			d->flags ^= D_SELECTED;
			GUI_EVENT(d, geTOGGLE);
			return (d->flags & D_EXIT) ? D_CLOSE : D_O_K;
	}
	return D_O_K;
}

static int32_t pressable(int32_t msg, DIALOG* d)
{
	switch (msg)
	{
		case MSG_WANTFOCUS:
			return D_WANTFOCUS;
		case MSG_KEY:
		case MSG_CLICK:
			GUI_EVENT(d, geCLICK);
			return (d->flags & D_EXIT) ? D_CLOSE : D_O_K;
	}
	return D_O_K;
}

int32_t jwin_iconbutton_proc(int32_t msg, DIALOG* d, int32_t)
{
	static const char* const labels[BTNICON_MAX] = {
		"^", "v", "<", ">", "-", "+", "-", "+", ">>", ">>>", "[]", "-", "+", "<<", "<<<", "*", "X", "^^", "^^^", "vv", "vvv", "@"
	};
	if (msg == MSG_ZCUI_KIND)
		return answer(K_BUTTON);
	if (msg == MSG_ZCUI_LABEL)
	{
		set_pending_text(d->d1 >= 0 && d->d1 < BTNICON_MAX ? labels[d->d1] : "");
		return D_O_K;
	}
	return pressable(msg, d);
}

int32_t jwin_infobtn_proc(int32_t msg, DIALOG* d, int32_t)
{
	if (msg == MSG_ZCUI_KIND)
		return answer(K_BUTTON);
	if (msg == MSG_ZCUI_LABEL)
	{
		set_pending_text("?");
		return D_O_K;
	}
	return pressable(msg, d);
}

// A window's per-frame callback. The dialog host steps once per interface frame,
// which can be faster than the game's 60 Hz, so the callback is paced here.
int32_t d_vsync_proc(int32_t msg, DIALOG* d, int32_t)
{
	if (msg != MSG_IDLE || !d->dp)
		return D_O_K;
	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	if (now - last < std::chrono::microseconds(16600))
		return D_O_K;
	last = now;
	auto& tick = *static_cast<GUI::function<int32_t()>*>(d->dp);
	switch (tick())
	{
		case ONTICK_EXIT:
		case ONTICK_CLOSE:
			return D_CLOSE;
		case ONTICK_REDRAW:
			return D_REDRAWME;
	}
	return D_O_K;
}

// ---- color swatch: a filled box that opens a 256-color palette to pick from --------

namespace
{
	const int CELL = 12, PICK_COLS = 16;

	int palette_grid_proc(int msg, DIALOG* d, int)
	{
		switch (msg)
		{
			case MSG_DRAW:
				for (int i = 0; i < 256; ++i)
				{
					const int x = d->x + (i % PICK_COLS) * CELL, y = d->y + (i / PICK_COLS) * CELL;
					rectfill(screen, x, y, x + CELL - 2, y + CELL - 2, i);
				}
				break;
			case MSG_CLICK:
			{
				const int col = (gui_mouse_x() - d->x) / CELL, row = (gui_mouse_y() - d->y) / CELL;
				if (col >= 0 && col < PICK_COLS && row >= 0 && row < PICK_COLS)
				{
					d->d1 = row * PICK_COLS + col;
					return D_CLOSE;
				}
				break;
			}
		}
		return D_O_K;
	}
}

int32_t jwin_color_swatch(int32_t msg, DIALOG* d, int32_t)
{
	switch (msg)
	{
		case MSG_DRAW:
			rectfill(screen, d->x, d->y, d->x + d->w - 1, d->y + d->h - 1, d->d1);
			rect(screen, d->x, d->y, d->x + d->w - 1, d->y + d->h - 1, scheme[jcBOXFG]);
			break;
		case MSG_WANTFOCUS:
			return D_WANTFOCUS;
		case MSG_CLICK:
		{
			if (d->flags & D_DISABLED)
				break;
			const int grid_size = CELL * PICK_COLS;
			DIALOG picker[] = {
				{jwin_win_proc, 0, 0, grid_size + 16, grid_size + 34, 0, 0, 0, D_EXIT, 0, 0, (void*)"Choose a Color", NULL, NULL},
				{palette_grid_proc, 8, 26, grid_size, grid_size, 0, 0, 0, 0, d->d1, 0, NULL, NULL, NULL},
				{d_keyboard_proc, 0, 0, 0, 0, 0, 0, 0, 0, KEY_ESC, KEY_ESC, (void*)close_dlg, NULL, NULL},
				{NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL},
			};
			jwin_center_dialog(picker);
			if (do_dialog(picker, -1) == 1)
			{
				d->d1 = picker[1].d1;
				GUI_EVENT(d, geCHANGE_VALUE);
			}
			return D_REDRAW;
		}
	}
	return D_O_K;
}
// ---- the "Loading Quest" box, shown by the port's message box ----------------------

void box_start(int32_t, const char* title, FONT*, FONT*, bool, int32_t, int32_t, uint8_t) { zcui_game::msgbox_start(title); }
void box_out(const char* msg) { zcui_game::msgbox_text(msg); }
void box_eol() { zcui_game::msgbox_eol(); }
void box_end(bool) { zcui_game::msgbox_end(); }
void box_save_x() {}
void box_load_x() {}

// ---- fullscreen, through the port's display settings ------------------------------

bool all_get_fullscreen_flag() { return zplayer_window_is_fullscreen() != 0; }
void all_toggle_fullscreen(bool on)
{
	if (on != all_get_fullscreen_flag())
		zplayer_toggle_fullscreen();
}

// ---- tab panels ----------------------------------------------------------------

int zcui_engine_tab_count(DIALOG* d) { return (int)static_cast<GUI::TabPanel*>(d->dp)->getSize(); }
const char* zcui_engine_tab_name(DIALOG* d, int i) { return static_cast<GUI::TabPanel*>(d->dp)->getName(i); }
int zcui_engine_tab_current(DIALOG* d) { return (int)static_cast<GUI::TabPanel*>(d->dp)->getCurrentIndex(); }
void zcui_engine_tab_select(DIALOG* d, int i) { static_cast<GUI::TabPanel*>(d->dp)->switchTo(i); }

// ---- scrolling panes and hover text ---------------------------------------------

namespace GUI { int32_t scrollProc(int32_t msg, DIALOG* d, int32_t c); }

bool zcui_engine_is_pane(DIALOG* d) { return d->proc == GUI::scrollProc; }
void zcui_engine_pane_state(DIALOG* d, int* x, int* max_x, int* y, int* max_y)
{
	auto* pane = static_cast<GUI::ScrollingPane*>(d->dp);
	*x = pane->portScrollX();
	*max_x = pane->portMaxX();
	*y = pane->portScrollY();
	*max_y = pane->portMaxY();
}
void zcui_engine_pane_scroll(DIALOG* d, int x, int y) { static_cast<GUI::ScrollingPane*>(d->dp)->portScrollTo(x, y); }

std::string zcui_engine_tooltip(DIALOG* d)
{
	GUI::Widget* widget = GUI::DialogRunner::findWidget(d);
	return widget ? widget->getTooltip() : std::string();
}

// ---- number fields that switch between decimal and hex -------------------------

namespace
{
	enum SwapField { SW_BYTE, SW_SSHORT, SW_ZSINT, SW_ZSINT_NODEC, SW_ZSINT2, SW_NOSWAP };
	enum SwapMode { MODE_DEC, MODE_HEX, MODE_LONG_DEC, MODE_LONG_HEX, MODE_BOOL };

	const int ZSINT_ONE = 10000;

	bool fixed_point(SwapField f) { return f == SW_ZSINT || f == SW_ZSINT2 || f == SW_NOSWAP; }

	GUI::TextField* field_of(DIALOG* d) { return static_cast<GUI::TextField*>(d->dp3); }
	int mode_of(DIALOG* d) { return field_of(d) ? field_of(d)->getSwapType() : MODE_DEC; }

	void hex_text(char* out, size_t n, int v)
	{
		if (v < 0)
			snprintf(out, n, "-%X", -v);
		else
			snprintf(out, n, "%X", v);
	}

	void show_value(SwapField f, DIALOG* d)
	{
		if (!d->dp)
			return;
		char text[32];
		const int v = d->fg, mode = mode_of(d);
		if (f == SW_ZSINT_NODEC)
		{
			if (mode == MODE_HEX)
				hex_text(text, sizeof(text), v / ZSINT_ONE);
			else
				snprintf(text, sizeof(text), "%d", v / ZSINT_ONE);
		}
		else if (!fixed_point(f))
		{
			if (mode == MODE_HEX)
				hex_text(text, sizeof(text), v);
			else
				snprintf(text, sizeof(text), "%d", v);
		}
		else if (mode == MODE_HEX)
			hex_text(text, sizeof(text), v / ZSINT_ONE);
		else if (mode == MODE_LONG_DEC)
			snprintf(text, sizeof(text), "%d", v);
		else if (mode == MODE_LONG_HEX)
			snprintf(text, sizeof(text), "%X", (unsigned)v);
		else if (mode == MODE_BOOL)
			snprintf(text, sizeof(text), "%d", v != 0 ? 1 : 0);
		else if (v % ZSINT_ONE == 0)
			snprintf(text, sizeof(text), "%d", v / ZSINT_ONE);
		else
		{
			const int a = abs(v);
			snprintf(text, sizeof(text), "%s%d.%04d", v < 0 ? "-" : "", a / ZSINT_ONE, a % ZSINT_ONE);
			for (size_t q = strlen(text) - 1; text[q] == '0'; --q)
				text[q] = 0;
		}
		const int room = d->d1 > 0 ? d->d1 : 0;
		strncpy((char*)d->dp, text, room);
		((char*)d->dp)[room] = 0;
	}

	int fixed_from_text(const char* s)
	{
		const bool negative = *s == '-';
		if (negative || *s == '+')
			++s;
		int whole = atoi(s), fraction = 0;
		if (const char* dot = strchr(s, '.'))
		{
			int scale = 1000;
			for (const char* p = dot + 1; *p >= '0' && *p <= '9' && scale > 0; ++p, scale /= 10)
				fraction += (*p - '0') * scale;
		}
		const int v = whole * ZSINT_ONE + fraction;
		return negative ? -v : v;
	}

	int value_from_text(SwapField f, int mode, const char* s)
	{
		int v;
		if (f == SW_ZSINT_NODEC)
			v = (mode == MODE_HEX ? (int)strtol(s, nullptr, 16) : atoi(s)) * ZSINT_ONE;
		else if (!fixed_point(f))
			v = mode == MODE_HEX ? (int)strtol(s, nullptr, 16) : atoi(s);
		else if (mode == MODE_HEX)
			v = (int)strtol(s, nullptr, 16) * ZSINT_ONE;
		else if (mode == MODE_LONG_DEC)
			v = atoi(s);
		else if (mode == MODE_LONG_HEX)
			v = (int)strtoul(s, nullptr, 16);
		else if (mode == MODE_BOOL)
			v = atoi(s) ? ZSINT_ONE : 0;
		else
			v = fixed_from_text(s);
		if (f == SW_BYTE)
			v = v < 0 ? 0 : v > 255 ? 255 : v;
		else if (f == SW_SSHORT)
			v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
		return v;
	}

	// The button's high nibble records the mode the text was last shown in.
	void follow_mode(SwapField f, DIALOG* d)
	{
		GUI::TextField* tf = field_of(d);
		if (!tf || !tf->isSwapType(true))
			return;
		DIALOG* button = d + 1;
		const int low = button->d1 & 0xF;
		if (low != ((button->d1 >> 4) & 0xF))
		{
			show_value(f, d);
			button->d1 = low | (low << 4);
		}
	}

	int32_t swap_field(SwapField f, int32_t msg, DIALOG* d, int32_t c)
	{
		switch (msg)
		{
			case MSG_ZCUI_KIND:
				return answer(K_EDIT); // hex digits and '.' need the unfiltered editor
			case MSG_START:
				show_value(f, d);
				if (d->dp)
					d->d2 = (int)strlen((char*)d->dp);
				break;
			case MSG_DRAW:
				follow_mode(f, d);
				break;
			case MSG_WANTFOCUS:
			case MSG_LOSTFOCUS:
			case MSG_KEY:
				return D_WANTFOCUS;
			case MSG_CHAR:
				if ((c >> 8) == KEY_ENTER)
				{
					GUI_EVENT(d, geENTER);
				}
				break;
			case MSG_ZCUI_TEXT:
				if (d->dp && !(d->flags & D_READONLY))
				{
					const char* s = pending_text();
					const int room = d->d1 > 0 ? d->d1 : 0;
					strncpy((char*)d->dp, s, room);
					((char*)d->dp)[room] = 0;
					d->d2 = (int)strlen((char*)d->dp);
					d->fg = value_from_text(f, mode_of(d), (char*)d->dp);
					GUI_EVENT(d, geUPDATE_SWAP);
				}
				break;
		}
		return D_O_K;
	}
}

#define SWAP_PROC(name, field) \
	int32_t name(int32_t msg, DIALOG* d, int32_t c) { return swap_field(field, msg, d, c); }
SWAP_PROC(jwin_numedit_swap_byte_proc, SW_BYTE)
SWAP_PROC(jwin_numedit_swap_sshort_proc, SW_SSHORT)
SWAP_PROC(jwin_numedit_swap_zsint_proc, SW_ZSINT)
SWAP_PROC(jwin_numedit_swap_zsint_nodec_proc, SW_ZSINT_NODEC)
SWAP_PROC(jwin_numedit_swap_zsint2_proc, SW_ZSINT2)
SWAP_PROC(jwin_numedit_noswap_zsint_proc, SW_NOSWAP)

int32_t jwin_swapbtn_proc(int32_t msg, DIALOG* d, int32_t)
{
	static const char* const labels[] = {"D", "H", "LD", "LH", "B"};
	switch (msg)
	{
		case MSG_ZCUI_KIND:
			return answer(K_BUTTON);
		case MSG_ZCUI_LABEL:
			set_pending_text(labels[(d->d1 & 0xF) % 5]);
			return D_O_K;
		case MSG_WANTFOCUS:
			return D_WANTFOCUS;
		case MSG_KEY:
		case MSG_CLICK:
		{
			DIALOG* edit = d - 1;
			if (auto* tf = field_of(edit))
			{
				tf->setSwapType((d->d1 & 0xF) + 1); // past the last mode wraps to the first
				object_message(edit, MSG_DRAW, 0);
			}
			GUI_EVENT(d, geUPDATE_SWAP);
			return D_O_K;
		}
	}
	return D_O_K;
}

// ---- flat drawing for menus and self-drawing controls -----------------------------

namespace
{
	inline int shade(int index) { return scheme[index]; }
	inline void hrun(BITMAP* b, int x1, int y, int x2, int c) { rectfill(b, x1, y, x2, y, c); }
	inline void vrun(BITMAP* b, int x, int y1, int y2, int c) { rectfill(b, x, y1, x, y2, c); }
}

void jwin_draw_frame(BITMAP* dest, int32_t x, int32_t y, int32_t w, int32_t h, int32_t style)
{
	if (style == FR_INVIS)
		return;
	int edge = jcMEDDARK, inner = jcBOX;
	if (style == FR_INV)
	{
		edge = jcDARK;
		inner = jcMEDDARK;
	}
	else if (style == FR_DEEP || style == FR_DARK)
		edge = inner = jcDARK;
	hrun(dest, x + 1, y, x + w - 2, shade(edge));
	hrun(dest, x + 1, y + h - 1, x + w - 2, shade(edge));
	vrun(dest, x, y + 1, y + h - 2, shade(edge));
	vrun(dest, x + w - 1, y + 1, y + h - 2, shade(edge));
	hrun(dest, x + 1, y + 1, x + w - 2, shade(inner));
	hrun(dest, x + 1, y + h - 2, x + w - 2, shade(inner));
	vrun(dest, x + 1, y + 2, y + h - 3, shade(inner));
	vrun(dest, x + w - 2, y + 2, y + h - 3, shade(inner));
}

void jwin_draw_win(BITMAP* dest, int32_t x, int32_t y, int32_t w, int32_t h, int32_t frame)
{
	rectfill(dest, x + 2, y + 2, x + w - 3, y + h - 3, shade(jcBOX));
	jwin_draw_frame(dest, x, y, w, h, frame);
}

void jwin_draw_button(BITMAP* dest, int32_t x, int32_t y, int32_t w, int32_t h, int32_t state, int32_t type)
{
	static const int by_state[4] = {FR_BOX, FR_INV, FR_DARK, FR_MEDDARK};
	jwin_draw_win(dest, x, y, w, h, (state == 0 && type == 1) ? (int)FR_WIN : by_state[state & 3]);
	if (state != 0)
		rectfill(dest, x + 1, y + 1, x + w - 2, y + h - 2, shade(jcMEDDARK));
}

void dotted_rect(BITMAP* dest, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t fg, int32_t bg)
{
	const int phase = (x1 + y1) & 1;
	for (int x = x1; x <= x2; ++x)
	{
		putpixel(dest, x, y1, ((x + y1) & 1) == phase ? fg : bg);
		putpixel(dest, x, y2, ((x + y2) & 1) == phase ? fg : bg);
	}
	for (int y = y1 + 1; y < y2; ++y)
	{
		putpixel(dest, x1, y, ((y + x1) & 1) == phase ? fg : bg);
		putpixel(dest, x2, y, ((y + x2) & 1) == phase ? fg : bg);
	}
}

void draw_arrow_button(BITMAP* dest, int32_t x, int32_t y, int32_t w, int32_t h, int32_t up, int32_t state)
{
	const int size = h / 3 < 5 ? h / 3 : 5;
	jwin_draw_button(dest, x, y, w, h, state, 1);
	const int cx = x + w / 2 - (state ? 0 : 1), top = y + (h - size) / 2 + (state ? 1 : 0);
	for (int i = 0; i < size; ++i)
	{
		const int half = up ? i : size - i - 1;
		hrun(dest, cx - half, top + i, cx + half, shade(jcBOXFG));
	}
}

void draw_arrow_button_horiz(BITMAP* dest, int32_t x, int32_t y, int32_t w, int32_t h, int32_t up, int32_t state)
{
	const int size = w / 3 < 5 ? w / 3 : 5;
	jwin_draw_button(dest, x, y, w, h, state, 1);
	const int cy = y + h / 2 - (state ? 0 : 1), left = x + (w - size) / 2 + (state ? 1 : 0);
	for (int i = 0; i < size; ++i)
	{
		const int half = up ? i : size - i - 1;
		vrun(dest, left + i, cy - half, cy + half, shade(jcBOXFG));
	}
}

void draw_arrow_horz(BITMAP* dest, int c, int x, int y, int w, bool left, bool center)
{
	const int top = center ? y - w : y;
	for (int i = 0; i < w; ++i)
	{
		const int half = left ? i : w - i - 1;
		vrun(dest, x + i, top + w - 1 - half, top + w - 1 + half, c);
	}
}

void draw_check(BITMAP* dest, int x1, int y1, int x2, int y2, int c)
{
	line(dest, x1, y1, x2, y2, c);
	line(dest, x1, y2, x2, y1, c);
}

// Multi-line text; '&' marks a shortcut letter and "&&" is a plain '&'. pos: 0 left,
// 1 centered, 2 right. Returns the widest line in pixels; a null bitmap only measures.
int32_t gui_textout_ln(BITMAP* bmp, FONT* f, const char* s, int32_t x, int32_t y, int32_t color, int32_t bg, int32_t pos)
{
	int widest = 0, line_y = y;
	while (*s)
	{
		char row[1024];
		int len = 0, underline = -1;
		for (; *s && *s != '\n' && len < (int)sizeof(row) - 1; ++s)
		{
			if (*s == '&')
			{
				if (s[1] != '&')
				{
					underline = len;
					continue;
				}
				++s;
			}
			row[len++] = *s;
		}
		row[len] = 0;
		if (*s == '\n')
			++s;
		const int width = text_length(f, row);
		if (width > widest)
			widest = width;
		const int left = x - (pos == 1 ? width / 2 : pos == 2 ? width : 0);
		if (bmp)
		{
			textout_ex(bmp, f, row, left, line_y, color, bg);
			if (underline >= 0 && underline < len)
			{
				char keep = row[underline + 1];
				row[underline + 1] = 0;
				const int end = text_length(f, row);
				row[underline] = 0;
				const int start = text_length(f, row);
				hline(bmp, left + start, line_y + text_height(f) - 1, left + end - 1, color);
				row[underline + 1] = keep;
			}
		}
		line_y += text_height(f);
	}
	return widest;
}

int32_t gui_text_width(FONT* f, const char* s) { return gui_textout_ln(nullptr, f, s, 0, 0, 0, -1, 0); }

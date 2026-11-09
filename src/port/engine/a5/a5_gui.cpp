// Color helpers and the debugger's ImGui back end, kept apart from a5_core.cpp
// because these need the engine's widget headers. The debugger window itself is
// connected in Phase 4 of UPSTREAM_INTEGRATION_PLAN.md.
#include "gui/jwin_a5.h"
#include "imgui_impl_allegro5.h"

#include <algorithm>

ALLEGRO_COLOR jwin_a5_colors[9];

ALLEGRO_COLOR jwin_a5_pal(int jc) { return jwin_a5_colors[jc]; }
void jwin_set_a5_colors(ALLEGRO_COLOR* colors) { std::copy(colors, colors + 9, jwin_a5_colors); }

ALLEGRO_COLOR a5color(RGB c) { return al_map_rgb(c.r, c.g, c.b); } // 8-bit palette values
ALLEGRO_COLOR a5color(int index)
{
	RGB c;
	get_color(index, &c);
	return a5color(c);
}

int32_t jwin_win_proc_a5(int32_t, DIALOG*, int32_t) { return D_O_K; }
void al_draw_int_rectangle(int, int, int, int, ALLEGRO_COLOR, double) {}

bool ImGui_ImplAllegro5_Init(ALLEGRO_DISPLAY*) { return false; }
void ImGui_ImplAllegro5_Shutdown() {}
void ImGui_ImplAllegro5_NewFrame() {}
void ImGui_ImplAllegro5_RenderDrawData(ImDrawData*) {}
bool ImGui_ImplAllegro5_ProcessEvent(ALLEGRO_EVENT*) { return false; }
bool ImGui_ImplAllegro5_CreateDeviceObjects() { return false; }
void ImGui_ImplAllegro5_InvalidateDeviceObjects() {}

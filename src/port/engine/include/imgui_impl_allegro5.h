// The calls the script debugger's window makes to its Dear ImGui back end. Written
// from scratch for zcport; implemented with the rest of src/port/engine/a5/.
#ifndef ZC_ENGINE_IMGUI_BACKEND_H
#define ZC_ENGINE_IMGUI_BACKEND_H

#include "imgui.h"
#include "allegro5/zc_a5_api.h"

bool ImGui_ImplAllegro5_Init(ALLEGRO_DISPLAY* display);
void ImGui_ImplAllegro5_Shutdown();
void ImGui_ImplAllegro5_NewFrame();
void ImGui_ImplAllegro5_RenderDrawData(ImDrawData* draw_data);
bool ImGui_ImplAllegro5_ProcessEvent(ALLEGRO_EVENT* ev);
bool ImGui_ImplAllegro5_CreateDeviceObjects();
void ImGui_ImplAllegro5_InvalidateDeviceObjects();

#endif

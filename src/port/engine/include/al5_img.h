// Image loading goes through zcport's loaders, registered at startup.
#ifndef ZC_ENGINE_AL5_IMG_H
#define ZC_ENGINE_AL5_IMG_H

extern "C" void zqeditor_register_image_loaders(void); // src/port/zqeditor/image_loaders.cpp
void zcui_dialogs_enable_canvases(float text_scale);   // src/port/game/ui_dialogs_host.cpp

inline void al5img_init() {}

// Called once during startup, so the other one-time dialog setup lives here too:
// the toolkit's self-drawing controls show as pictures in the dialog host.
inline void register_png_file_type()
{
	zqeditor_register_image_loaders();
	zcui_dialogs_enable_canvases(1.0f);
}

#endif

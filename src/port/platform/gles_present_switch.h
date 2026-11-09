// zplayer on Nintendo Switch: presentation through EGL and OpenGL ES 3 (the
// switch-mesa portlib). One path draws the game's picture and Dear ImGui.
//
// Every function must be called on the thread that called init().
//
// Written from scratch for zcport.

#ifndef ZCPORT_GLES_PRESENT_SWITCH_H
#define ZCPORT_GLES_PRESENT_SWITCH_H

#include <stdint.h>
#include <string>

struct ImDrawData;

namespace zcgles
{

// Creates the context on the default window, 'width' x 'height'.
bool init(void *native_window, int width, int height, std::string *error);
void shutdown();

// Replaces the game's picture: w x h pixels, bytes R, G, B, A in memory.
void set_picture(const uint32_t *rgba, int w, int h);

// Draws the picture into the given rectangle (dst_w <= 0: no picture) and
// 'ui' over it (may be NULL), then swaps, which waits for the display.
void present(ImDrawData *ui, int dst_x, int dst_y, int dst_w, int dst_h, bool smooth);

} // namespace zcgles

#endif

// sdl_present.h - shows finished game frames through SDL's own renderer
// (Direct3D 11/12, OpenGL, OpenGL ES, Metal, or software), for computers
// where Vulkan is missing or doesn't work.
//
// Same job and interface as zcport::VulkanPresenter (vk_present.h): it
// takes the finished frame (0xAARRGGBB), draws it scaled into the window,
// draws the Dear ImGui menus and dialogs over it and presents. Placement
// and filtering match the Vulkan presenter, so the picture looks the same.
//
// Written from scratch for zcport.

#ifndef ZC_PORT_SDL_PRESENT_H
#define ZC_PORT_SDL_PRESENT_H

#include <stdint.h>
#include <string>

#include "vk_present.h"   // zcport::ScaleMode

struct SDL_Window;
struct ImDrawData;

namespace zcport
{

class SdlPresenter
{
public:
    SdlPresenter();
    ~SdlPresenter();

    // 'drivers': SDL render driver names, comma separated (e.g. "software"),
    // or NULL for SDL's own order without its Vulkan renderer. On failure
    // returns false and fills *error.
    bool init(SDL_Window *window, const char *drivers, bool vsync, std::string *error);
    void shutdown();

    // Draw one frame: w x h pixels of 0xAARRGGBB, pitch in pixels; 'ui' is
    // drawn over it. Returns false when the renderer can't go on (device
    // lost).
    bool present(const uint32_t *pixels, int w, int h, int pitch, ScaleMode mode,
                 ImDrawData *ui = 0);

    // Dear ImGui renderer: after init(), with an ImGui context current.
    bool init_ui();
    void shutdown_ui();
    bool ui_ready() const;
    void ui_new_frame();

    void set_vsync(bool vsync);
    void notify_resized();

    // SDL's render events (SDL_EVENT_RENDER_*): textures are made again
    // after a device reset; a lost device makes present() fail.
    void device_reset();
    void device_lost();

    // SDL's name for the driver in use ("direct3d11", "opengl", "software").
    const char *driver_name() const;

private:
    struct Impl;
    Impl *impl;

    SdlPresenter(const SdlPresenter &);
    SdlPresenter &operator=(const SdlPresenter &);
};

// "Direct3D 11", "OpenGL", ... for an SDL render driver name.
const char *render_driver_title(const char *driver);

} // namespace zcport

#endif

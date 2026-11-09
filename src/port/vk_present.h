// vk_present.h - shows finished game frames in an SDL3 window with Vulkan.
//
// The player still renders every frame in software (8-bit bitmaps + palette,
// exactly as before). This module only takes the finished frame, converted to
// BGRA, and puts it on screen: upload to a texture, draw it scaled into the
// window, present. Vulkan 1.0 core + VK_KHR_swapchain only, so it runs on
// Windows and Android alike.

#ifndef ZC_PORT_VK_PRESENT_H
#define ZC_PORT_VK_PRESENT_H

#include <stdint.h>
#include <string>

struct SDL_Window;

struct ImDrawData;

namespace zcport
{

enum ScaleMode
{
    SCALE_INTEGER,        // largest whole-number scale that fits, centred (pixel exact)
    SCALE_ASPECT,         // fill as much as possible keeping the aspect ratio, sharp pixels
    SCALE_ASPECT_SMOOTH,  // same, with bilinear filtering
    SCALE_STRETCH         // fill the whole window
};

class VulkanPresenter
{
public:
    VulkanPresenter();
    ~VulkanPresenter();

    // The window must have been created with SDL_WINDOW_VULKAN after
    // SDL_Vulkan_LoadLibrary(). On failure returns false and fills *error.
    bool init(SDL_Window *window, bool vsync, std::string *error);
    void shutdown();

    // Draw one frame: w x h pixels of 0xAARRGGBB, pitch in pixels.
    // Returns false only on an unrecoverable device error.
    // 'ui', when given, is a Dear ImGui frame drawn over the picture (see
    // init_ui()).
    bool present(const uint32_t *pixels, int w, int h, int pitch, ScaleMode mode,
                 ImDrawData *ui = 0);

    // Dear ImGui renderer (zplayer's menus and dialogs): after init(), with
    // an ImGui context current. ui_new_frame() before ImGui::NewFrame().
    bool init_ui();
    void shutdown_ui();
    bool ui_ready() const;
    void ui_new_frame();

    // Vertical sync on/off; takes effect with the next frame.
    void set_vsync(bool vsync);

    // Call when the window was resized or its surface changed.
    void notify_resized();

    const char *device_name() const;

private:
    struct Impl;
    Impl *impl;

    VulkanPresenter(const VulkanPresenter &);
    VulkanPresenter &operator=(const VulkanPresenter &);
};

} // namespace zcport

#endif

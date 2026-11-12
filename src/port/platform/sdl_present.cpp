// sdl_present.cpp - see sdl_present.h.
//
// Written from scratch for zcport.

#include "sdl_present.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

namespace zcport
{

namespace
{

// Same placement as vk_present.cpp's dest_rect.
void dest_rect(int ew, int eh, int w, int h, ScaleMode mode,
               float *x0, float *y0, float *x1, float *y1)
{
    float dw = (float)ew, dh = (float)eh;
    if(w <= 0 || h <= 0 || ew <= 0 || eh <= 0)
    {
        *x0 = *y0 = 0;
        *x1 = (float)ew;
        *y1 = (float)eh;
        return;
    }
    if(mode == SCALE_INTEGER)
    {
        int s = std::min(ew / w, eh / h);
        if(s < 1)
            mode = SCALE_ASPECT;   // window smaller than the frame
        else
        {
            dw = (float)(w * s);
            dh = (float)(h * s);
        }
    }
    if(mode == SCALE_ASPECT || mode == SCALE_ASPECT_SMOOTH)
    {
        float s = std::min((float)ew / w, (float)eh / h);
        dw = w * s;
        dh = h * s;
    }
    *x0 = (float)(int)((ew - dw) / 2);
    *y0 = (float)(int)((eh - dh) / 2);
    *x1 = *x0 + dw;
    *y1 = *y0 + dh;
}

SDL_Texture *tex_of(ImTextureID id)
{
    return (SDL_Texture *)(intptr_t)id;
}

} // namespace

const char *render_driver_title(const char *d)
{
    if(!d || !*d)
        return "none";
    if(!SDL_strcmp(d, "direct3d11")) return "Direct3D 11";
    if(!SDL_strcmp(d, "direct3d12")) return "Direct3D 12";
    if(!SDL_strcmp(d, "direct3d"))   return "Direct3D 9";
    if(!SDL_strcmp(d, "metal"))      return "Metal";
    if(!SDL_strcmp(d, "opengl"))     return "OpenGL";
    if(!SDL_strcmp(d, "opengles2"))  return "OpenGL ES 2";
    if(!SDL_strcmp(d, "vulkan"))     return "Vulkan (SDL)";
    if(!SDL_strcmp(d, "gpu"))        return "SDL GPU";
    if(!SDL_strcmp(d, "software"))   return "Software";
    return d;
}

struct SdlPresenter::Impl
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *picture;
    int pic_w, pic_h;
    bool vsync;
    bool ui;
    bool lost;
    std::string name;
    std::vector<float> xy;            // UI vertices in output pixels
    std::vector<SDL_FColor> colors;   // ... and their colours
    std::vector<unsigned char> rgba;  // Alpha8 textures widened to RGBA

    Impl()
        : window(NULL), renderer(NULL), picture(NULL), pic_w(0), pic_h(0),
          vsync(false), ui(false), lost(false)
    {
    }

    // ImGui's texture requests (ImGuiBackendFlags_RendererHasTextures): the
    // font atlas and the dialogs' pictures.
    const void *pixels_rgba(ImTextureData *tex, int x, int y, int w, int h, int *pitch)
    {
        if(tex->Format == ImTextureFormat_RGBA32)
        {
            *pitch = tex->GetPitch();
            return tex->GetPixelsAt(x, y);
        }
        rgba.resize((size_t)w * h * 4);
        for(int j = 0; j < h; j++)
        {
            const unsigned char *src = (const unsigned char *)tex->GetPixelsAt(x, y + j);
            unsigned char *dst = &rgba[(size_t)j * w * 4];
            for(int i = 0; i < w; i++)
            {
                dst[i * 4 + 0] = dst[i * 4 + 1] = dst[i * 4 + 2] = 255;
                dst[i * 4 + 3] = src[i];
            }
        }
        *pitch = w * 4;
        return &rgba[0];
    }

    void destroy_texture(ImTextureData *tex)
    {
        SDL_Texture *t = tex_of(tex->GetTexID());
        if(t)
            SDL_DestroyTexture(t);
        tex->SetTexID(ImTextureID_Invalid);
        tex->BackendUserData = NULL;
        tex->SetStatus(ImTextureStatus_Destroyed);
    }

    void update_texture(ImTextureData *tex)
    {
        if(tex->Status == ImTextureStatus_WantCreate)
        {
            SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                               tex->Width, tex->Height);
            if(!t)
            {
                SDL_Log("zcport: SDL renderer: a %dx%d UI texture failed (%s)", tex->Width, tex->Height,
                        SDL_GetError());
                return;   // asked again next frame
            }
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
            int pitch = 0;
            const void *px = pixels_rgba(tex, 0, 0, tex->Width, tex->Height, &pitch);
            SDL_UpdateTexture(t, NULL, px, pitch);
            tex->SetTexID((ImTextureID)(intptr_t)t);
            tex->SetStatus(ImTextureStatus_OK);
        }
        else if(tex->Status == ImTextureStatus_WantUpdates)
        {
            SDL_Texture *t = tex_of(tex->GetTexID());
            for(int i = 0; t && i < tex->Updates.Size; i++)
            {
                const ImTextureRect &r = tex->Updates[i];
                SDL_Rect rect = { r.x, r.y, r.w, r.h };
                int pitch = 0;
                const void *px = pixels_rgba(tex, r.x, r.y, r.w, r.h, &pitch);
                SDL_UpdateTexture(t, &rect, px, pitch);
            }
            tex->SetStatus(ImTextureStatus_OK);
        }
        else if(tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0)
            destroy_texture(tex);
    }

    void draw_ui(ImDrawData *dd, int ow, int oh)
    {
        if(dd->Textures)
        {
            for(int i = 0; i < dd->Textures->Size; i++)
            {
                ImTextureData *tex = (*dd->Textures)[i];
                if(tex->Status != ImTextureStatus_OK)
                    update_texture(tex);
            }
        }

        // ImGui lays out in window units (io.DisplaySize); the output is in
        // pixels. Positions and clip rectangles are scaled here, so the
        // renderer's own scale stays 1.
        const float sx = dd->DisplaySize.x > 0 ? ow / dd->DisplaySize.x : 1.0f;
        const float sy = dd->DisplaySize.y > 0 ? oh / dd->DisplaySize.y : 1.0f;
        const ImVec2 off = dd->DisplayPos;
        for(int n = 0; n < dd->CmdListsCount; n++)
        {
            const ImDrawList *list = dd->CmdLists[n];
            const int nv = list->VtxBuffer.Size;
            if(nv <= 0)
                continue;
            xy.resize((size_t)nv * 2);
            colors.resize((size_t)nv);
            const ImDrawVert *v = list->VtxBuffer.Data;
            for(int i = 0; i < nv; i++)
            {
                xy[i * 2 + 0] = (v[i].pos.x - off.x) * sx;
                xy[i * 2 + 1] = (v[i].pos.y - off.y) * sy;
                const ImU32 c = v[i].col;
                colors[i].r = ((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
                colors[i].g = ((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
                colors[i].b = ((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
                colors[i].a = ((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
            }
            const float *uv = (const float *)((const char *)v + offsetof(ImDrawVert, uv));
            for(int c = 0; c < list->CmdBuffer.Size; c++)
            {
                const ImDrawCmd &cmd = list->CmdBuffer[c];
                if(cmd.UserCallback)
                {
                    if(cmd.UserCallback != ImDrawCallback_ResetRenderState)
                        cmd.UserCallback(list, &cmd);
                    continue;
                }
                float x0 = (cmd.ClipRect.x - off.x) * sx, y0 = (cmd.ClipRect.y - off.y) * sy;
                float x1 = (cmd.ClipRect.z - off.x) * sx, y1 = (cmd.ClipRect.w - off.y) * sy;
                x0 = std::max(x0, 0.0f);
                y0 = std::max(y0, 0.0f);
                x1 = std::min(x1, (float)ow);
                y1 = std::min(y1, (float)oh);
                if(x1 <= x0 || y1 <= y0 || cmd.ElemCount == 0)
                    continue;
                SDL_Texture *t = tex_of(cmd.GetTexID());
                if(!t)
                    continue;   // its texture couldn't be made
                SDL_Rect clip = { (int)x0, (int)y0, (int)(x1 - x0 + 0.5f), (int)(y1 - y0 + 0.5f) };
                SDL_SetRenderClipRect(renderer, &clip);
                const unsigned vo = cmd.VtxOffset;
                SDL_RenderGeometryRaw(renderer, t,
                                      &xy[(size_t)vo * 2], (int)(sizeof(float) * 2),
                                      &colors[vo], (int)sizeof(SDL_FColor),
                                      (const float *)((const char *)uv + (size_t)vo * sizeof(ImDrawVert)),
                                      (int)sizeof(ImDrawVert),
                                      nv - (int)vo,
                                      list->IdxBuffer.Data + cmd.IdxOffset, (int)cmd.ElemCount,
                                      (int)sizeof(ImDrawIdx));
            }
        }
        SDL_SetRenderClipRect(renderer, NULL);
    }
};

SdlPresenter::SdlPresenter() : impl(new Impl)
{
}

SdlPresenter::~SdlPresenter()
{
    shutdown();
    delete impl;
}

bool SdlPresenter::init(SDL_Window *window, const char *drivers, bool vsync, std::string *error)
{
    shutdown();
    std::string list;
    if(drivers && *drivers)
        list = drivers;
    else
    {
        // SDL's own order (Direct3D 11 first on Windows, Metal on macOS,
        // OpenGL elsewhere), without its Vulkan renderer: this presenter is
        // what runs when Vulkan doesn't.
        const int n = SDL_GetNumRenderDrivers();
        for(int i = 0; i < n; i++)
        {
            const char *d = SDL_GetRenderDriver(i);
            if(!d || !SDL_strcmp(d, "vulkan"))
                continue;
            if(!list.empty())
                list += ",";
            list += d;
        }
    }
    impl->renderer = SDL_CreateRenderer(window, list.empty() ? NULL : list.c_str());
    if(!impl->renderer)
    {
        if(error)
            *error = SDL_GetError();
        return false;
    }
    impl->window = window;
    impl->vsync = vsync;
    impl->lost = false;
    const char *name = SDL_GetRendererName(impl->renderer);
    impl->name = name ? name : "";
    SDL_SetRenderVSync(impl->renderer, vsync ? 1 : 0);
    SDL_Log("zcport: SDL renderer: %s", render_driver_title(impl->name.c_str()));
    return true;
}

void SdlPresenter::shutdown()
{
    shutdown_ui();
    if(impl->picture)
    {
        SDL_DestroyTexture(impl->picture);
        impl->picture = NULL;
    }
    impl->pic_w = impl->pic_h = 0;
    if(impl->renderer)
    {
        SDL_DestroyRenderer(impl->renderer);
        impl->renderer = NULL;
    }
    impl->window = NULL;
    impl->name.clear();
}

bool SdlPresenter::present(const uint32_t *pixels, int w, int h, int pitch, ScaleMode mode, ImDrawData *ui)
{
    SDL_Renderer *r = impl->renderer;
    if(!r || impl->lost)
        return false;
    if(!pixels || w <= 0 || h <= 0)
        return true;
    if(!impl->picture || impl->pic_w != w || impl->pic_h != h)
    {
        if(impl->picture)
            SDL_DestroyTexture(impl->picture);
        impl->picture = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        if(!impl->picture)
        {
            SDL_Log("zcport: SDL renderer: the %dx%d picture texture failed (%s)", w, h, SDL_GetError());
            impl->pic_w = impl->pic_h = 0;
            return false;
        }
        SDL_SetTextureBlendMode(impl->picture, SDL_BLENDMODE_NONE);
        impl->pic_w = w;
        impl->pic_h = h;
    }
    SDL_UpdateTexture(impl->picture, NULL, pixels, pitch * 4);
    // Sharp pixels except for "Fit (smooth)", as the Vulkan presenter.
    SDL_SetTextureScaleMode(impl->picture, mode == SCALE_ASPECT_SMOOTH ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);

    SDL_SetRenderViewport(r, NULL);
    SDL_SetRenderClipRect(r, NULL);
    SDL_SetRenderScale(r, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(r, &ow, &oh);
    float x0, y0, x1, y1;
    dest_rect(ow, oh, w, h, mode, &x0, &y0, &x1, &y1);
    SDL_FRect dst = { x0, y0, x1 - x0, y1 - y0 };
    SDL_RenderTexture(r, impl->picture, NULL, &dst);
    if(ui && impl->ui)
        impl->draw_ui(ui, ow, oh);
    if(!SDL_RenderPresent(r))
        return !impl->lost;
    return true;
}

bool SdlPresenter::init_ui()
{
    if(!impl->renderer)
        return false;
    if(impl->ui)
        return true;
    ImGuiIO &io = ImGui::GetIO();
    if(io.BackendRendererUserData)
        return false;   // another renderer still holds the UI
    io.BackendRendererUserData = impl;
    io.BackendRendererName = "zcport SDL renderer";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    impl->ui = true;
    return true;
}

void SdlPresenter::shutdown_ui()
{
    if(!impl->ui)
        return;
    // The UI's textures go with this renderer; ImGui makes them again for
    // whichever renderer takes over.
    ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
    for(int i = 0; i < pio.Textures.Size; i++)
    {
        ImTextureData *tex = pio.Textures[i];
        if(tex->RefCount == 1 && tex->GetTexID() != ImTextureID_Invalid)
            impl->destroy_texture(tex);
    }
    ImGuiIO &io = ImGui::GetIO();
    io.BackendRendererUserData = NULL;
    io.BackendRendererName = NULL;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    impl->ui = false;
}

bool SdlPresenter::ui_ready() const
{
    return impl->ui;
}

void SdlPresenter::ui_new_frame()
{
}

void SdlPresenter::set_vsync(bool vsync)
{
    impl->vsync = vsync;
    if(impl->renderer)
        SDL_SetRenderVSync(impl->renderer, vsync ? 1 : 0);
}

void SdlPresenter::notify_resized()
{
    // SDL's renderer follows the window's size by itself.
}

void SdlPresenter::device_reset()
{
    // The device's textures are gone: the picture is made again at the next
    // frame, the UI's when ImGui sees them destroyed.
    if(impl->picture)
    {
        SDL_DestroyTexture(impl->picture);
        impl->picture = NULL;
    }
    impl->pic_w = impl->pic_h = 0;
    if(impl->ui)
    {
        ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
        for(int i = 0; i < pio.Textures.Size; i++)
        {
            ImTextureData *tex = pio.Textures[i];
            if(tex->RefCount == 1 && tex->GetTexID() != ImTextureID_Invalid)
                impl->destroy_texture(tex);
        }
    }
}

void SdlPresenter::device_lost()
{
    impl->lost = true;
}

const char *SdlPresenter::driver_name() const
{
    return impl->name.c_str();
}

} // namespace zcport

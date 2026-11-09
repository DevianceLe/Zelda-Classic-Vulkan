// vk_present_test: checks the SDL3 + Vulkan presentation path on its own.
//
// Renders an 8-bit 320x240 test frame with a cycling 256-colour palette (like
// the game's own frames), converts it to BGRA and shows it with
// zcport::VulkanPresenter for a few seconds, then writes a short report.
//
// usage: vk_present_test [--display N] [--seconds S] [--log file]

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "vk_present.h"

// Move the window to screen N counting from the left (same rule as the
// player's window_display setting). 0 leaves it alone.
static void place_on_display(SDL_Window *win, int display)
{
    if(display <= 0)
        return;
    int count = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&count);
    if(!ids || display > count)
    {
        SDL_free(ids);
        return;
    }
    std::vector<std::pair<int, SDL_DisplayID> > order;
    for(int i = 0; i < count; i++)
    {
        SDL_Rect r;
        if(SDL_GetDisplayBounds(ids[i], &r))
            order.push_back(std::make_pair(r.x * 65536 + r.y, ids[i]));
    }
    SDL_free(ids);
    std::sort(order.begin(), order.end());
    if(display > (int)order.size())
        return;
    SDL_DisplayID id = order[display - 1].second;
    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED_DISPLAY(id), SDL_WINDOWPOS_CENTERED_DISPLAY(id));
}

int main(int argc, char *argv[])
{
    int display = 3;
    double seconds = 6.0;
    std::string log_path = "vk_present_test.log";
    for(int i = 1; i + 1 < argc; i++)
    {
        if(!strcmp(argv[i], "--display")) display = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--seconds")) seconds = atof(argv[++i]);
        else if(!strcmp(argv[i], "--log")) log_path = argv[++i];
    }

    FILE *log = fopen(log_path.c_str(), "w");
    if(!log)
        return 1;

    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        fprintf(log, "SDL_Init failed: %s\n", SDL_GetError());
        fclose(log);
        return 1;
    }
    if(!SDL_Vulkan_LoadLibrary(NULL))
    {
        fprintf(log, "SDL_Vulkan_LoadLibrary failed: %s\n", SDL_GetError());
        fclose(log);
        SDL_Quit();
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("zplayer Vulkan test", 640, 480, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if(!win)
    {
        fprintf(log, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        fclose(log);
        SDL_Quit();
        return 1;
    }
    place_on_display(win, display);

    zcport::VulkanPresenter presenter;
    std::string err;
    if(!presenter.init(win, true, &err))
    {
        fprintf(log, "Vulkan init failed: %s\n", err.c_str());
        fclose(log);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    fprintf(log, "SDL %d.%d.%d, video driver %s\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION, SDL_GetCurrentVideoDriver());
    fprintf(log, "GPU: %s\n", presenter.device_name());

    const int W = 320, H = 240;
    std::vector<unsigned char> frame8(W * H);
    std::vector<uint32_t> bgra(W * H);
    uint32_t palette[256];

    Uint64 start = SDL_GetTicksNS();
    int frames = 0;
    bool running = true, ok = true;
    while(running)
    {
        SDL_Event ev;
        while(SDL_PollEvent(&ev))
        {
            if(ev.type == SDL_EVENT_QUIT)
                running = false;
            else if(ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || ev.type == SDL_EVENT_WINDOW_RESIZED)
                presenter.notify_resized();
        }

        double t = (SDL_GetTicksNS() - start) / 1e9;
        if(t >= seconds)
            break;

        // 8-bit frame: 16x15 grid of palette indices, plus a moving block.
        for(int y = 0; y < H; y++)
            for(int x = 0; x < W; x++)
                frame8[y * W + x] = (unsigned char)((x / 20) + (y / 16) * 16);
        int bx = (int)(t * 60) % (W - 32), by = 104;
        for(int y = by; y < by + 32; y++)
            for(int x = bx; x < bx + 32; x++)
                frame8[y * W + x] = 255;

        // Cycling palette (like a palette fade); entry 255 stays white.
        for(int i = 0; i < 255; i++)
        {
            double h = fmod(i / 255.0 + t * 0.1, 1.0) * 6.0;
            int seg = (int)h;
            double f = h - seg;
            int up = (int)(255 * f), down = 255 - up;
            int r = 0, g = 0, b = 0;
            switch(seg)
            {
            case 0: r = 255; g = up; break;
            case 1: r = down; g = 255; break;
            case 2: g = 255; b = up; break;
            case 3: g = down; b = 255; break;
            case 4: r = up; b = 255; break;
            default: r = 255; b = down; break;
            }
            palette[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
        palette[255] = 0xFFFFFFFFu;

        for(int i = 0; i < W * H; i++)
            bgra[i] = palette[frame8[i]];

        if(!presenter.present(&bgra[0], W, H, W, zcport::SCALE_INTEGER))
        {
            ok = false;
            break;
        }
        frames++;
    }

    double elapsed = (SDL_GetTicksNS() - start) / 1e9;
    int ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(win, &ww, &wh);
    int wx = 0, wy = 0;
    SDL_GetWindowPosition(win, &wx, &wy);
    fprintf(log, "window %dx%d at %d,%d\n", ww, wh, wx, wy);
    fprintf(log, "presented %d frames in %.2f s (%.1f fps)\n", frames, elapsed, frames / (elapsed > 0 ? elapsed : 1));
    fprintf(log, "%s\n", ok ? "RESULT: OK" : "RESULT: present failed");
    fclose(log);

    presenter.shutdown();
    SDL_DestroyWindow(win);
    SDL_Vulkan_UnloadLibrary();
    SDL_Quit();
    return ok ? 0 : 1;
}

/*
 * test_platform_runtime_sdl_portonly.cpp - runtime checks of the SDL3 +
 * Vulkan platform that the reference build has no counterpart for:
 * what is presented (read back from the platform), synthetic SDL input
 * reaching key[] / readkey() / mouse_x / joy[], the input thread, window
 * placement, display switching, fullscreen scaling and the hooks other
 * modules call. A window appears briefly on screen 3 counting from the left
 * (if there is one).
 */
#include "conf.h"

#include <allegro/internal/aintern.h>
#include <winalleg.h>
#include "test_platform_shims.h"
#include "zcport/core.h"
#include "zcport/platform.h"
#include "zcport/platform_sdl.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace
{
    typedef std::chrono::steady_clock clk;

    double ms_since(clk::time_point t0)
    {
        return std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    }

    void plain_wait(double ms)
    {
        clk::time_point t0 = clk::now();
        while(ms_since(t0) < ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    unsigned expand6(int v) { v &= 63; return (unsigned)((v << 2) | (v >> 4)); }

    unsigned argb(const RGB &c)
    {
        return 0xFF000000u | (expand6(c.r) << 16) | (expand6(c.g) << 8) | expand6(c.b);
    }

    void window_event(SDL_EventType type);

    /* Keeps the real pointer out of the window, so only the synthetic
     * events of the tests move mouse_x / mouse_y. */
    void park_pointer()
    {
        HWND hwnd = win_get_window();
        if(!hwnd)
            return;
        RECT wr;
        GetWindowRect(hwnd, &wr);
        SetCursorPos(wr.right + 60, wr.bottom + 60);
        window_event(SDL_EVENT_WINDOW_MOUSE_LEAVE);
    }

    bool open_window(int w, int h)
    {
        if(set_gfx_mode(GFX_AUTODETECT_WINDOWED, w, h, 0, 0) != 0)
            return false;
        set_display_switch_mode(SWITCH_BACKGROUND);
        zcport_place_window_on_display(3);
        park_pointer();
        return true;
    }

    /* Waits until a frame newer than 'after' was presented. */
    bool wait_frame(unsigned long after, int timeout_ms = 1000)
    {
        clk::time_point t0 = clk::now();
        while(ms_since(t0) < timeout_ms)
        {
            if(zcport_sdl_frames_presented() > after)
                return true;
            rest(1);
        }
        return false;
    }

    /* Hands the screen over now and waits until it is presented. */
    bool show_now()
    {
        unsigned long n = zcport_sdl_frames_presented();
        vsync();
        rest(20);
        return wait_frame(n);
    }

    bool last_frame(std::vector<uint32_t> *px, int *w, int *h)
    {
        px->resize(2048 * 2048);
        return zcport_sdl_last_frame(&(*px)[0], (int)px->size(), w, h) != 0;
    }

    void push(SDL_Event &e)
    {
        SDL_PushEvent(&e);
    }

    void key_event(SDL_Scancode sc, bool down, SDL_Keymod mod = SDL_KMOD_NONE, bool repeat = false)
    {
        SDL_Event e;
        SDL_zero(e);
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.windowID = zcport_sdl_window_id();
        e.key.scancode = sc;
        e.key.key = SDL_GetKeyFromScancode(sc, mod, true);
        e.key.mod = mod;
        e.key.down = down;
        e.key.repeat = repeat;
        e.key.timestamp = SDL_GetTicksNS();
        push(e);
    }

    void tap(SDL_Scancode sc, SDL_Keymod mod = SDL_KMOD_NONE)
    {
        key_event(sc, true, mod);
        key_event(sc, false, mod);
        zcport_sdl_sync();
    }

    void window_event(SDL_EventType type)
    {
        SDL_Event e;
        SDL_zero(e);
        e.type = type;
        e.window.windowID = zcport_sdl_window_id();
        e.window.timestamp = SDL_GetTicksNS();
        push(e);
        zcport_sdl_sync();
    }

    void mouse_move(float x, float y)
    {
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = zcport_sdl_window_id();
        e.motion.x = x;
        e.motion.y = y;
        e.motion.xrel = 1;
        e.motion.yrel = 1;
        e.motion.timestamp = SDL_GetTicksNS();
        push(e);
        zcport_sdl_sync();
    }

    void mouse_button(Uint8 b, bool down, float x, float y)
    {
        SDL_Event e;
        SDL_zero(e);
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID = zcport_sdl_window_id();
        e.button.button = b;
        e.button.down = down;
        e.button.clicks = 1;
        e.button.x = x;
        e.button.y = y;
        e.button.timestamp = SDL_GetTicksNS();
        push(e);
        zcport_sdl_sync();
    }

    void mouse_wheel(float y)
    {
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.windowID = zcport_sdl_window_id();
        e.wheel.y = y;
        e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        e.wheel.timestamp = SDL_GetTicksNS();
        push(e);
        zcport_sdl_sync();
    }

    struct ScreenList
    {
        HMONITOR mon[16];
        RECT rc[16];
        int count;
    };

    BOOL CALLBACK collect(HMONITOR m, HDC, LPRECT, LPARAM lp)
    {
        ScreenList *l = (ScreenList *)lp;
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if(l->count < 16 && GetMonitorInfo(m, &mi))
        {
            l->mon[l->count] = m;
            l->rc[l->count] = mi.rcMonitor;
            l->count++;
        }
        return TRUE;
    }
}

CONF_TEST(rt_platform_registered)
{
    const ZCPORT_PLATFORM *p = zcport_get_platform();
    ctx.expect("registered", p != NULL && p->name && strcmp(p->name, "SDL3") == 0, "%s", p && p->name ? p->name : "(none)");
    ctx.expect("installed", system_driver != NULL, "no system driver");
}

CONF_TEST(rt_window_and_vulkan)
{
    install_timer();
    if(!open_window(320, 240))
    {
        ctx.fail("set_gfx_mode", "%s", allegro_error);
        return;
    }
    ctx.expect("hwnd", win_get_window() != NULL, "no HWND");
    ctx.expect("vulkan", zcport_sdl_using_vulkan() == 1, "using_vulkan=%d", zcport_sdl_using_vulkan());
    ctx.expect("window_id", zcport_sdl_window_id() != 0, "0");

    ScreenList l;
    l.count = 0;
    EnumDisplayMonitors(NULL, NULL, collect, (LPARAM)&l);
    if(l.count >= 3)
    {
        for(int i = 1; i < l.count; i++)
            for(int j = i; j > 0 && (l.rc[j - 1].left > l.rc[j].left ||
                                     (l.rc[j - 1].left == l.rc[j].left && l.rc[j - 1].top > l.rc[j].top)); j--)
            {
                HMONITOR tm = l.mon[j]; l.mon[j] = l.mon[j - 1]; l.mon[j - 1] = tm;
                RECT tr = l.rc[j]; l.rc[j] = l.rc[j - 1]; l.rc[j - 1] = tr;
            }
        HMONITOR on = MonitorFromWindow(win_get_window(), MONITOR_DEFAULTTONULL);
        ctx.expect("on_screen_3", on == l.mon[2], "window is not on the third screen from the left");
        RECT wr;
        GetWindowRect(win_get_window(), &wr);
        int cx = (wr.left + wr.right) / 2, cy = (wr.top + wr.bottom) / 2;
        int mx = (l.rc[2].left + l.rc[2].right) / 2;
        ctx.expect("centred", cx > mx - 60 && cx < mx + 60, "centre %d,%d, screen centre x %d", cx, cy, mx);
    }
    else
        ctx.note("fewer than 3 screens: placement not checked");

    /* Screen 99 does not exist: the window stays. */
    RECT before, after;
    GetWindowRect(win_get_window(), &before);
    zcport_place_window_on_display(99);
    GetWindowRect(win_get_window(), &after);
    ctx.expect("bad_screen_keeps_place", before.left == after.left && before.top == after.top, "moved");
}

CONF_TEST(rt_frame_through_palette)
{
    install_timer();
    if(!open_window(320, 240))
    {
        ctx.fail("set_gfx_mode", "%s", allegro_error);
        return;
    }
    PALETTE pal;
    for(int i = 0; i < 256; i++)
    {
        pal[i].r = (unsigned char)(i & 63);
        pal[i].g = (unsigned char)((i * 7 + 3) & 63);
        pal[i].b = (unsigned char)(63 - (i & 63));
        pal[i].filler = 0;
    }
    set_palette(pal);
    for(int y = 0; y < 240; y++)
        for(int x = 0; x < 320; x++)
            putpixel(screen, x, y, ((y / 15) * 16 + x / 20) & 255);
    ctx.expect("presented", show_now(), "no frame presented");

    std::vector<uint32_t> px;
    int w = 0, h = 0;
    if(!last_frame(&px, &w, &h))
    {
        ctx.fail("readback", "no frame");
        return;
    }
    ctx.expect("size", w == 320 && h == 240, "%dx%d", w, h);
    int bad = 0, first = -1;
    for(int y = 0; y < 240 && w == 320; y++)
        for(int x = 0; x < 320; x++)
        {
            int idx = ((y / 15) * 16 + x / 20) & 255;
            if(px[(size_t)y * 320 + x] != argb(pal[idx]))
            {
                if(first < 0)
                    first = y * 320 + x;
                bad++;
            }
        }
    ctx.expect("pixels", bad == 0, "%d pixels differ, first at %d,%d", bad, first % 320, first / 320);

    /* A palette change alone shows up. */
    pal[0].r = 63;
    pal[0].g = 0;
    pal[0].b = 0;
    set_palette(pal);
    show_now();
    last_frame(&px, &w, &h);
    ctx.expect("palette_change", px[0] == 0xFFFF0000u, "%08x", (unsigned)px[0]);

    /* 32-bit screen */
    set_color_depth(32);
    if(open_window(320, 240))
    {
        clear_to_color(screen, 0x123456);
        putpixel(screen, 5, 5, 0xABCDEF);
        show_now();
        last_frame(&px, &w, &h);
        ctx.expect("rgb32", px[0] == 0xFF123456u && px[5 * 320 + 5] == 0xFFABCDEFu, "%08x %08x",
                   (unsigned)px[0], (unsigned)px[5 * 320 + 5]);
    }
    set_color_depth(8);
}

CONF_TEST(rt_mouse_pointer_composited)
{
    install_timer();
    install_mouse();
    if(!open_window(320, 240))
    {
        ctx.fail("set_gfx_mode", "%s", allegro_error);
        remove_mouse();
        return;
    }
    PALETTE pal;
    for(int i = 0; i < 256; i++)
    {
        pal[i].r = (unsigned char)(i & 63);
        pal[i].g = (unsigned char)((i >> 2) & 63);
        pal[i].b = 20;
        pal[i].filler = 0;
    }
    set_palette(pal);
    clear_to_color(screen, 3);
    BITMAP *spr = create_bitmap_ex(8, 4, 4);
    clear_to_color(spr, 9);
    putpixel(spr, 0, 0, 0);          /* transparent (mask colour) */
    set_mouse_sprite(spr);
    set_mouse_sprite_focus(1, 1);
    show_mouse(screen);
    mouse_move(101.0f, 51.0f);        /* the window is 1:1: display (101, 51) */
    ctx.expect("mouse_pos", mouse_x == 101 && mouse_y == 51, "%d,%d", mouse_x, mouse_y);
    show_now();
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    last_frame(&px, &w, &h);
    /* sprite top-left at (100, 50); (100,50) is transparent */
    ctx.expect("pointer_drawn", px[51 * 320 + 101] == argb(pal[9]) && px[53 * 320 + 103] == argb(pal[9]),
               "%08x", (unsigned)px[51 * 320 + 101]);
    ctx.expect("mask_transparent", px[50 * 320 + 100] == argb(pal[3]), "%08x", (unsigned)px[50 * 320 + 100]);
    ctx.expect("screen_untouched", getpixel(screen, 101, 51) == 3, "%d", getpixel(screen, 101, 51));

    scare_mouse();
    show_now();
    last_frame(&px, &w, &h);
    ctx.expect("scared_hidden", px[51 * 320 + 101] == argb(pal[3]), "%08x", (unsigned)px[51 * 320 + 101]);
    unscare_mouse();
    show_mouse(NULL);
    show_now();
    last_frame(&px, &w, &h);
    ctx.expect("hidden", px[51 * 320 + 101] == argb(pal[3]), "%08x", (unsigned)px[51 * 320 + 101]);
    set_mouse_sprite(NULL);
    destroy_bitmap(spr);
    remove_mouse();
}

CONF_TEST(rt_busy_loop_still_presents)
{
    install_timer();
    if(!open_window(320, 240))
    {
        ctx.fail("set_gfx_mode", "%s", allegro_error);
        return;
    }
    show_now();
    clear_to_color(screen, 0);
    /* The game draws and then busy-waits without calling Allegro. */
    putpixel(screen, 10, 10, 15);
    unsigned long n0 = zcport_sdl_frames_presented();
    clk::time_point t0 = clk::now();
    while(ms_since(t0) < 400)
    {
    }
    unsigned long n1 = zcport_sdl_frames_presented();
    ctx.expect("presented_meanwhile", n1 > n0, "%lu -> %lu", n0, n1);
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    last_frame(&px, &w, &h);
    ctx.expect("shows_new_content", px[10 * 320 + 10] == argb(_current_palette[15]), "%08x", (unsigned)px[10 * 320 + 10]);
}

CONF_TEST(rt_frame_rate_limit)
{
    install_timer();
    if(!open_window(320, 240))
    {
        ctx.fail("set_gfx_mode", "%s", allegro_error);
        return;
    }
    /* keypressed() in a tight loop: frames are handed over at most once
     * per refresh, and the loop is not slowed down. */
    install_keyboard();
    unsigned long n0 = zcport_sdl_frames_presented();
    long calls = 0;
    clk::time_point t0 = clk::now();
    while(ms_since(t0) < 1000)
    {
        keypressed();
        calls++;
    }
    unsigned long n = zcport_sdl_frames_presented() - n0;
    ctx.note("1 s of keypressed(): %ld calls, %lu frames", calls, n);
    ctx.expect("frames", n >= 20 && n <= 260, "%lu", n);
    ctx.expect("calls", calls > 50000, "%ld", calls);
    remove_keyboard();
}

CONF_TEST(rt_keyboard)
{
    install_timer();
    install_keyboard();
    if(!open_window(320, 240))
    {
        ctx.fail("set_gfx_mode", "%s", allegro_error);
        remove_keyboard();
        return;
    }
    clear_keybuf();
    SDL_Keycode ka = SDL_GetKeyFromScancode(SDL_SCANCODE_A, SDL_KMOD_NONE, false);
    key_event(SDL_SCANCODE_A, true);
    zcport_sdl_sync();
    ctx.expect("key_down", key[KEY_A] != 0, "key[KEY_A]=%d", key[KEY_A]);
    ctx.expect("keypressed", keypressed() != 0, "empty buffer");
    int k = readkey();
    ctx.expect("readkey_a", k == ((int)ka | (KEY_A << 8)), "%04x (layout gives %04x)", k, (unsigned)ka);
    key_event(SDL_SCANCODE_A, true, SDL_KMOD_NONE, true);   /* OS repeat: ignored */
    zcport_sdl_sync();
    ctx.expect("os_repeat_ignored", !keypressed(), "buffered");
    key_event(SDL_SCANCODE_A, false);
    zcport_sdl_sync();
    ctx.expect("key_up", key[KEY_A] == 0, "%d", key[KEY_A]);

    SDL_Keycode kA = SDL_GetKeyFromScancode(SDL_SCANCODE_A, SDL_KMOD_LSHIFT, false);
    key_event(SDL_SCANCODE_LSHIFT, true, SDL_KMOD_LSHIFT);
    zcport_sdl_sync();
    ctx.expect("shift_flag", (key_shifts & KB_SHIFT_FLAG) != 0, "%04x", key_shifts);
    ctx.expect("shift_no_char", !keypressed(), "modifier buffered");
    tap(SDL_SCANCODE_A, SDL_KMOD_LSHIFT);
    k = readkey();
    ctx.expect("readkey_shift_a", k == ((int)kA | (KEY_A << 8)), "%04x", k);
    key_event(SDL_SCANCODE_LSHIFT, false);
    zcport_sdl_sync();

    tap(SDL_SCANCODE_C, SDL_KMOD_LCTRL);
    k = readkey();
    SDL_Keycode kc = SDL_GetKeyFromScancode(SDL_SCANCODE_C, SDL_KMOD_NONE, false);
    if(kc >= 'a' && kc <= 'z')
        ctx.expect("ctrl_c", k == ((int)(kc - 'a' + 1) | (KEY_C << 8)), "%04x", k);
    tap(SDL_SCANCODE_F1);
    k = readkey();
    ctx.expect("f1", k == (KEY_F1 << 8), "%04x", k);
    tap(SDL_SCANCODE_RETURN);
    ctx.expect("enter", readkey() == (13 | (KEY_ENTER << 8)), "");
    tap(SDL_SCANCODE_ESCAPE);
    ctx.expect("esc", readkey() == (27 | (KEY_ESC << 8)), "");
    tap(SDL_SCANCODE_BACKSPACE);
    ctx.expect("backspace", readkey() == (8 | (KEY_BACKSPACE << 8)), "");
    tap(SDL_SCANCODE_UP);
    ctx.expect("up", readkey() == (KEY_UP << 8), "");
    tap(SDL_SCANCODE_KP_5, SDL_KMOD_NUM);
    k = readkey();
    ctx.expect("pad5_numlock", k == ('5' | (KEY_5_PAD << 8)), "%04x", k);
    ctx.expect("numlock_flag", (key_shifts & KB_NUMLOCK_FLAG) != 0, "%04x", key_shifts);
    tap(SDL_SCANCODE_KP_5);
    k = readkey();
    ctx.expect("pad5_no_numlock", k == (KEY_5_PAD << 8), "%04x", k);
    tap(SDL_SCANCODE_X, SDL_KMOD_LALT);
    k = readkey();
    ctx.expect("alt_x", k == (KEY_X << 8), "%04x", k);
    tap(SDL_SCANCODE_SPACE, SDL_KMOD_CAPS);
    ctx.expect("capslock_flag", (key_shifts & KB_CAPSLOCK_FLAG) != 0, "%04x", key_shifts);
    ctx.expect("space", readkey() == (' ' | (KEY_SPACE << 8)), "");

    /* key[] changes while the game busy-waits on it (as with Allegro's
     * input thread: src/title.cpp waits like this for Esc to be released). */
    key_event(SDL_SCANCODE_B, true);
    std::atomic<bool> released(false);
    std::thread t([&released] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        key_event(SDL_SCANCODE_B, false);
        released = true;
    });
    clk::time_point t0 = clk::now();
    while(!key[KEY_B] && ms_since(t0) < 1000)
    {
    }
    bool saw_down = key[KEY_B] != 0;
    while(key[KEY_B] && ms_since(t0) < 2000)
    {
    }
    t.join();
    ctx.expect("busy_wait_down", saw_down, "never down");
    ctx.expect("busy_wait_up", key[KEY_B] == 0, "never released");

    /* Focus loss releases the keys. */
    key_event(SDL_SCANCODE_D, true);
    zcport_sdl_sync();
    window_event(SDL_EVENT_WINDOW_FOCUS_GAINED);
    window_event(SDL_EVENT_WINDOW_FOCUS_LOST);
    ctx.expect("focus_lost_releases", key[KEY_D] == 0, "%d", key[KEY_D]);
    window_event(SDL_EVENT_WINDOW_FOCUS_GAINED);
    rest(1);
    clear_keybuf();
    remove_keyboard();
}

CONF_TEST(rt_mouse_input)
{
    install_timer();
    install_mouse();
    if(!open_window(320, 240))
    {
        ctx.fail("set_gfx_mode", "%s", allegro_error);
        remove_mouse();
        return;
    }
    mouse_move(37.6f, 200.2f);
    ctx.expect("pos", mouse_x == 37 && mouse_y == 200, "%d,%d", mouse_x, mouse_y);
    mouse_move(-5.0f, 400.0f);
    ctx.expect("clamped", mouse_x == 0 && mouse_y == 239, "%d,%d", mouse_x, mouse_y);
    mouse_button(SDL_BUTTON_LEFT, true, 10, 10);
    ctx.expect("left_down", (mouse_b & 1) != 0 && mouse_x == 10 && mouse_y == 10, "b=%d at %d,%d", mouse_b, mouse_x, mouse_y);
    mouse_button(SDL_BUTTON_RIGHT, true, 10, 10);
    ctx.expect("right_down", (mouse_b & 3) == 3, "b=%d", mouse_b);
    mouse_button(SDL_BUTTON_MIDDLE, true, 10, 10);
    ctx.expect("middle_down", (mouse_b & 7) == 7, "b=%d", mouse_b);
    mouse_button(SDL_BUTTON_LEFT, false, 10, 10);
    mouse_button(SDL_BUTTON_RIGHT, false, 10, 10);
    mouse_button(SDL_BUTTON_MIDDLE, false, 10, 10);
    ctx.expect("all_up", mouse_b == 0, "b=%d", mouse_b);
    int z0 = mouse_z;
    mouse_wheel(1.0f);
    ctx.expect("wheel_up", mouse_z == z0 + 1, "%d", mouse_z - z0);
    mouse_wheel(-1.0f);
    mouse_wheel(-1.0f);
    ctx.expect("wheel_down", mouse_z == z0 - 1, "%d", mouse_z - z0);
    mouse_wheel(0.5f);
    mouse_wheel(0.5f);
    ctx.expect("wheel_fraction", mouse_z == z0, "%d", mouse_z - z0);

    /* The mouse range follows the screen of a new mode; the pointer is
     * centred. */
    open_window(640, 480);
    ctx.expect("centred", mouse_x == 320 && mouse_y == 240, "%d,%d", mouse_x, mouse_y);
    mouse_move(630.0f, 470.0f);
    ctx.expect("pos_640", mouse_x == 630 && mouse_y == 470, "%d,%d", mouse_x, mouse_y);
    remove_mouse();
}

CONF_TEST(rt_fullscreen_scaling)
{
    /* Fullscreen shows the screen scaled on the window's display (screen 3
     * when there is one); the mouse maps back through the same scaling. */
    install_timer();
    install_mouse();
    open_window(320, 240);
    int r = set_gfx_mode(GFX_AUTODETECT_FULLSCREEN, 320, 240, 0, 0);
    ctx.check_int("set_fullscreen", r);
    if(r != 0)
    {
        remove_mouse();
        return;
    }
    set_display_switch_mode(SWITCH_BACKGROUND);
    ctx.expect("not_windowed", !is_windowed_mode(), "windowed");
    ctx.expect("screen", screen && screen->w == 320 && screen->h == 240, "");
    plain_wait(300);
    int pw = 0, ph = 0;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    zcport_sdl_window_geometry(&pw, &ph, &x0, &y0, &x1, &y1);
    ctx.note("fullscreen %dx%d, image at %.1f,%.1f - %.1f,%.1f", pw, ph, x0, y0, x1, y1);
    ctx.expect("fills_height_or_width", (y1 - y0) >= ph - 1 || (x1 - x0) >= pw - 1, "image %.0fx%.0f in %dx%d",
               x1 - x0, y1 - y0, pw, ph);
    float sx = (x1 - x0) / 320.0f, sy = (y1 - y0) / 240.0f;
    mouse_move(x0 + sx * 100.5f, y0 + sy * 60.5f);
    ctx.expect("mouse_mapped", mouse_x == 100 && mouse_y == 60, "%d,%d", mouse_x, mouse_y);

    /* vsync() waits for the retrace in fullscreen modes. */
    vsync();
    clk::time_point t0 = clk::now();
    for(int i = 0; i < 10; i++)
        vsync();
    double d = ms_since(t0) / 10;
    ctx.note("fullscreen vsync %.2f ms", d);
    ctx.expect("vsync_waits", d >= 3.0 && d <= 35.0, "%.2f ms", d);

    r = set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    ctx.check_int("back_to_windowed", r);
    set_display_switch_mode(SWITCH_BACKGROUND);
    zcport_place_window_on_display(3);
    ctx.expect("windowed_again", is_windowed_mode() != 0, "");
    RECT rc;
    GetClientRect(win_get_window(), &rc);
    ctx.expect("client_320", rc.right - rc.left == 320 && rc.bottom - rc.top == 240, "%dx%d",
               (int)(rc.right - rc.left), (int)(rc.bottom - rc.top));
    remove_mouse();
}

namespace
{
    volatile int g_switch_in = 0, g_switch_out = 0;
    std::thread::id g_switch_thread;
    void on_switch_in() { g_switch_in = g_switch_in + 1; g_switch_thread = std::this_thread::get_id(); }
    void on_switch_out() { g_switch_out = g_switch_out + 1; }
    volatile int g_close = 0;
    void on_close() { g_close = g_close + 1; }
}

CONF_TEST(rt_display_switching)
{
    install_timer();
    if(!open_window(320, 240))
    {
        ctx.fail("set_gfx_mode", "%s", allegro_error);
        return;
    }
    g_switch_in = g_switch_out = 0;
    set_display_switch_mode(SWITCH_BACKGROUND);
    set_display_switch_callback(SWITCH_IN, on_switch_in);
    set_display_switch_callback(SWITCH_OUT, on_switch_out);
    window_event(SDL_EVENT_WINDOW_FOCUS_GAINED);
    window_event(SDL_EVENT_WINDOW_FOCUS_LOST);
    rest(1);
    ctx.expect("switch_out_called", g_switch_out == 1, "%d", g_switch_out);
    window_event(SDL_EVENT_WINDOW_FOCUS_GAINED);
    rest(1);
    ctx.expect("switch_in_called", g_switch_in == 1, "%d", g_switch_in);
    ctx.expect("on_game_thread", g_switch_thread == std::this_thread::get_id(), "other thread");

    /* SWITCH_PAUSE: the game stops while switched out. */
    set_display_switch_mode(SWITCH_PAUSE);
    window_event(SDL_EVENT_WINDOW_FOCUS_LOST);
    std::thread t([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        e.window.windowID = zcport_sdl_window_id();
        SDL_PushEvent(&e);
    });
    clk::time_point t0 = clk::now();
    rest(1);
    double d = ms_since(t0);
    t.join();
    ctx.note("paused for %.0f ms", d);
    ctx.expect("paused", d >= 250, "%.0f ms", d);
    set_display_switch_mode(SWITCH_BACKGROUND);
}

CONF_TEST(rt_close_button)
{
    install_timer();
    if(!open_window(320, 240))
    {
        ctx.fail("set_gfx_mode", "%s", allegro_error);
        return;
    }
    g_close = 0;
    set_close_button_callback(on_close);
    window_event(SDL_EVENT_WINDOW_CLOSE_REQUESTED);
    ctx.expect("callback", g_close == 1, "%d", g_close);
    set_close_button_callback(NULL);
    window_event(SDL_EVENT_WINDOW_CLOSE_REQUESTED);
    ctx.expect("no_callback", g_close == 1, "%d", g_close);
    ctx.expect("window_alive", IsWindow(win_get_window()) != 0, "closed");

    /* The hook the foundation is to call from set_close_button_callback(). */
    const ZCPORT_PLATFORM *p = zcport_get_platform();
    if(p && p->close_button_enable)
    {
        HMENU m = GetSystemMenu(win_get_window(), FALSE);
        p->close_button_enable(0);
        zcport_sdl_sync();
        ctx.expect("greyed", (GetMenuState(m, SC_CLOSE, MF_BYCOMMAND) & MF_GRAYED) != 0, "not greyed");
        p->close_button_enable(1);
        zcport_sdl_sync();
        ctx.expect("enabled", (GetMenuState(m, SC_CLOSE, MF_BYCOMMAND) & MF_GRAYED) == 0, "greyed");
    }
    else
        ctx.fail("hook", "close_button_enable missing");
}

CONF_TEST(rt_hw_cursor_caps)
{
    /* The helper the foundation's show_mouse() is to call. */
    install_timer();
    if(!open_window(320, 240))
    {
        ctx.fail("set_gfx_mode", "%s", allegro_error);
        return;
    }
    zcport_gfx_mouse_shown(screen);
    ctx.expect("set", (gfx_capabilities & GFX_HW_CURSOR) != 0, "%08x", gfx_capabilities);
    BITMAP *b = create_bitmap(8, 8);
    zcport_gfx_mouse_shown(b);
    ctx.expect("other", (gfx_capabilities & GFX_HW_CURSOR) == 0, "%08x", gfx_capabilities);
    zcport_gfx_mouse_shown(screen);
    zcport_gfx_mouse_shown(NULL);
    ctx.expect("cleared", (gfx_capabilities & GFX_HW_CURSOR) == 0, "%08x", gfx_capabilities);
    ctx.expect("rest_kept", gfx_capabilities == 0x07b30088, "%08x", gfx_capabilities);
    destroy_bitmap(b);
}

CONF_TEST(rt_joystick_virtual)
{
    install_timer();
    if(!open_window(320, 240))
    {
        ctx.fail("set_gfx_mode", "%s", allegro_error);
        return;
    }
    install_joystick(JOY_TYPE_AUTODETECT);
    poll_joystick();
    int before = num_joysticks;

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_UNKNOWN;
    desc.naxes = 4;
    desc.nbuttons = 6;
    desc.nhats = 1;
    desc.name = "zcport test stick";
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if(!id)
    {
        ctx.fail("attach", "%s", SDL_GetError());
        remove_joystick();
        return;
    }
    SDL_Joystick *js = SDL_OpenJoystick(id);
    SDL_SetJoystickVirtualAxis(js, 0, -32768);
    SDL_SetJoystickVirtualAxis(js, 1, 20000);
    SDL_SetJoystickVirtualAxis(js, 2, 100);
    SDL_SetJoystickVirtualAxis(js, 3, 32767);
    SDL_SetJoystickVirtualButton(js, 2, true);
    SDL_SetJoystickVirtualHat(js, 0, SDL_HAT_LEFTUP);

    clk::time_point t0 = clk::now();
    int n = -1;
    while(ms_since(t0) < 2000)
    {
        poll_joystick();
        n = num_joysticks - 1;
        if(num_joysticks > before && joy[n].button[2].b && joy[n].stick[0].axis[0].pos == -128)
            break;
        rest(5);
    }
    ctx.expect("added", num_joysticks == before + 1, "%d -> %d", before, num_joysticks);
    if(n >= 0)
    {
        JOYSTICK_INFO &j = joy[n];
        ctx.expect("buttons", j.num_buttons == 6 && j.button[2].b && !j.button[0].b, "n=%d b2=%d", j.num_buttons, j.button[2].b);
        ctx.expect("sticks", j.num_sticks == 3, "%d", j.num_sticks);
        ctx.expect("x_left", j.stick[0].axis[0].pos == -128 && j.stick[0].axis[0].d1 && !j.stick[0].axis[0].d2,
                   "pos %d d1 %d d2 %d", j.stick[0].axis[0].pos, j.stick[0].axis[0].d1, j.stick[0].axis[0].d2);
        ctx.expect("y_down", j.stick[0].axis[1].pos == 78 && j.stick[0].axis[1].d2 && !j.stick[0].axis[1].d1,
                   "pos %d", j.stick[0].axis[1].pos);
        ctx.expect("stick2", j.stick[1].num_axis == 2 && j.stick[1].axis[0].pos == 0 && j.stick[1].axis[1].pos == 127,
                   "%d %d", j.stick[1].axis[0].pos, j.stick[1].axis[1].pos);
        ctx.expect("hat", j.stick[2].axis[0].pos == -128 && j.stick[2].axis[0].d1 && j.stick[2].axis[1].pos == -128 &&
                   j.stick[2].axis[1].d1, "%d %d", j.stick[2].axis[0].pos, j.stick[2].axis[1].pos);
        ctx.expect("flags", (j.stick[0].flags & (JOYFLAG_ANALOGUE | JOYFLAG_DIGITAL | JOYFLAG_SIGNED)) ==
                   (JOYFLAG_ANALOGUE | JOYFLAG_DIGITAL | JOYFLAG_SIGNED), "%x", j.stick[0].flags);
    }
    SDL_SetJoystickVirtualButton(js, 2, false);
    t0 = clk::now();
    while(ms_since(t0) < 1000 && n >= 0 && joy[n].button[2].b)
    {
        poll_joystick();
        rest(5);
    }
    ctx.expect("button_released", n >= 0 && !joy[n].button[2].b, "");
    SDL_CloseJoystick(js);
    SDL_DetachVirtualJoystick(id);
    t0 = clk::now();
    while(ms_since(t0) < 1000 && num_joysticks != before)
    {
        poll_joystick();
        rest(5);
    }
    ctx.expect("removed", num_joysticks == before, "%d", num_joysticks);
    remove_joystick();
}

CONF_TEST(rt_headless_without_platform)
{
    /* Without a platform (zcport_set_platform(NULL)) a graphics mode is a
     * memory screen that is never shown. */
    install_timer();
    set_gfx_mode(GFX_TEXT, 0, 0, 0, 0);
    const ZCPORT_PLATFORM *p = zcport_get_platform();
    zcport_set_platform(NULL);
    int r = set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    ctx.expect("set", r == 0 && screen && screen->w == 320, "%d", r);
    putpixel(screen, 1, 1, 5);
    vsync();
    rest(1);
    ctx.expect("draws", getpixel(screen, 1, 1) == 5, "");
    ctx.expect("no_window", win_get_window() == NULL, "");
    set_gfx_mode(GFX_TEXT, 0, 0, 0, 0);
    zcport_set_platform(p);
}

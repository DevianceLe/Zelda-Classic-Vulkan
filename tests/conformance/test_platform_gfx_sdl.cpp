/*
 * test_platform_gfx_sdl.cpp - graphics modes, the screen bitmap, the window
 * (platform module).
 *
 * Opens a real window (windowed modes only: a fullscreen mode would change
 * the desktop resolution on the reference side). The window is moved to
 * screen 3 counting from the left when there is one, with the same Win32
 * calls on both sides (win_get_window() gives the HWND).
 *
 * Window properties (client size, style, title, close button) are read
 * through Win32 on both sides. What the window shows is captured from the
 * screen (desktop DC, after the window is moved on top) and compared as
 * colours.
 */
#include "conf.h"

#include <allegro/internal/aintern.h>   /* _gfx_mode_set_count (gui.cpp reads it) */
#include <winalleg.h>
#include "test_platform_shims.h"

#include <chrono>
#include <string>
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

    struct ScreenList
    {
        RECT work[16];
        int count;
    };

    BOOL CALLBACK collect_screens(HMONITOR mon, HDC, LPRECT, LPARAM lp)
    {
        ScreenList *list = (ScreenList *)lp;
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if(list->count < 16 && GetMonitorInfo(mon, &mi))
            list->work[list->count++] = mi.rcWork;
        return TRUE;
    }

    /* Screen 3 from the left if there is one, else the last one. Also sets
     * SWITCH_BACKGROUND (windowed modes start in SWITCH_PAUSE, which would
     * stop the program while another window has the focus). */
    void place_window(HWND hwnd)
    {
        set_display_switch_mode(SWITCH_BACKGROUND);
        if(!hwnd)
            return;
        ScreenList list;
        list.count = 0;
        EnumDisplayMonitors(NULL, NULL, collect_screens, (LPARAM)&list);
        if(list.count == 0)
            return;
        for(int i = 1; i < list.count; i++)
            for(int j = i; j > 0 && list.work[j - 1].left > list.work[j].left; j--)
            {
                RECT t = list.work[j];
                list.work[j] = list.work[j - 1];
                list.work[j - 1] = t;
            }
        int n = list.count >= 3 ? 2 : list.count - 1;
        const RECT &w = list.work[n];
        RECT wr;
        GetWindowRect(hwnd, &wr);
        int x = w.left + ((w.right - w.left) - (wr.right - wr.left)) / 2;
        int y = w.top + ((w.bottom - w.top) - (wr.bottom - wr.top)) / 2;
        SetWindowPos(hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
    }

    std::string window_title(HWND hwnd)
    {
        wchar_t buf[256];
        int n = GetWindowTextW(hwnd, buf, 256);
        char out[1024];
        int m = WideCharToMultiByte(CP_UTF8, 0, buf, n, out, sizeof(out) - 1, NULL, NULL);
        out[m < 0 ? 0 : m] = 0;
        return out;
    }

    void check_window(conf::Ctx &ctx, const char *c)
    {
        HWND hwnd = win_get_window();
        if(!hwnd)
        {
            ctx.checkf(c, "no window");
            return;
        }
        RECT rc;
        GetClientRect(hwnd, &rc);
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        LONG mask = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
                    WS_THICKFRAME | WS_POPUP | WS_VISIBLE | WS_DISABLED;
        ctx.checkf(c, "client %dx%d style %08lx visible %d",
                   (int)(rc.right - rc.left), (int)(rc.bottom - rc.top),
                   (unsigned long)(style & mask), IsWindowVisible(hwnd) ? 1 : 0);
    }

    int sysmenu_close_state(HWND hwnd)
    {
        HMENU m = GetSystemMenu(hwnd, FALSE);
        if(!m)
            return -1;
        UINT st = GetMenuState(m, SC_CLOSE, MF_BYCOMMAND);
        if(st == (UINT)-1)
            return -2;
        return (st & (MF_GRAYED | MF_DISABLED)) ? 1 : 0;
    }

    /* Hash of the screen through getpixel(). */
    conf::u64 hash_screen()
    {
        conf::Hash h;
        h.add_i32(screen->w);
        h.add_i32(screen->h);
        for(int y = 0; y < screen->h; y++)
            for(int x = 0; x < screen->w; x++)
                h.add_u32((conf::u32)getpixel(screen, x, y));
        return h.value();
    }
}

CONF_TEST(gfx_windowed_basic)
{
    install_timer();
    int before = _gfx_mode_set_count;
    ctx.check_int("set", set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0));
    ctx.check_int("mode_set_count_delta", _gfx_mode_set_count - before);
    ctx.check_bool("screen", screen != NULL);
    if(!screen)
        return;
    place_window(win_get_window());

    ctx.check_int("screen_w", screen->w);
    ctx.check_int("screen_h", screen->h);
    ctx.check_int("depth", bitmap_color_depth(screen));
    ctx.check_int("SCREEN_W", SCREEN_W);
    ctx.check_int("SCREEN_H", SCREEN_H);
    ctx.check_int("VIRTUAL_W", VIRTUAL_W);
    ctx.check_int("VIRTUAL_H", VIRTUAL_H);
    ctx.check_clip("clip", screen);
    ctx.check_bool("is_screen_bitmap", is_screen_bitmap(screen) != 0);
    ctx.check_bool("is_video_bitmap", is_video_bitmap(screen) != 0);
    ctx.check_bool("is_memory_bitmap", is_memory_bitmap(screen) != 0);
    ctx.check_bool("is_system_bitmap", is_system_bitmap(screen) != 0);
    ctx.check_bool("is_sub_bitmap", is_sub_bitmap(screen) != 0);
    ctx.check_bool("is_windowed", is_windowed_mode() != 0);
    ctx.check_hex("caps", (unsigned)gfx_capabilities);
    ctx.check_bool("driver", gfx_driver != NULL);
    if(gfx_driver)
    {
        ctx.check_hex("driver_id", (unsigned)gfx_driver->id);
        ctx.check_int("driver_w", gfx_driver->w);
        ctx.check_int("driver_h", gfx_driver->h);
        ctx.check_int("driver_windowed", gfx_driver->windowed);
        ctx.note("driver name '%s' desc '%s' ascii '%s' linear %d vid_mem %ld",
                 gfx_driver->name, gfx_driver->desc, gfx_driver->ascii_name,
                 gfx_driver->linear, gfx_driver->vid_mem);
    }
    ctx.check_hash("cleared", hash_screen());
    ctx.check_int("refresh_rate", get_refresh_rate());
    check_window(ctx, "window");
    ctx.note("title '%s'", window_title(win_get_window()).c_str());

    /* The palette after set_gfx_mode. */
    PALETTE pal;
    get_palette(pal);
    ctx.check_palette("palette", pal);
    ctx.check_palette("default_palette", default_palette);
    ctx.check_bool("palette_is_default", conf::hash_palette(pal) == conf::hash_palette(default_palette));
}

CONF_TEST(gfx_window_props)
{
    install_timer();
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    HWND hwnd = win_get_window();
    if(!hwnd)
    {
        ctx.fail("window", "none");
        return;
    }
    place_window(hwnd);
    ctx.check_int("close_grayed_default", sysmenu_close_state(hwnd));

    set_window_title("conformance title");
    ctx.check_str("title", window_title(hwnd).c_str());
    set_window_title("");
    ctx.check_str("title_empty", window_title(hwnd).c_str());
    set_window_title("Zelda Classic");
    ctx.check_str("title_zc", window_title(hwnd).c_str());
}

static volatile int g_close_hits = 0;
static void close_cb(void) { g_close_hits = g_close_hits + 1; }

CONF_TEST(gfx_close_button)
{
    install_timer();
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    HWND hwnd = win_get_window();
    if(!hwnd)
    {
        ctx.fail("window", "none");
        return;
    }
    place_window(hwnd);
    ctx.check_int("set_cb", set_close_button_callback(close_cb));
    ctx.check_int("close_grayed_with_cb", sysmenu_close_state(hwnd));
    g_close_hits = 0;
    PostMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
    plain_wait(300);
    rest(1);
    ctx.check_int("cb_called", g_close_hits);
    ctx.check_bool("window_alive", IsWindow(hwnd) != 0);
    ctx.check_int("unset_cb", set_close_button_callback(NULL));
    ctx.check_int("close_grayed_without_cb", sysmenu_close_state(hwnd));
    PostMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
    plain_wait(300);
    rest(1);
    ctx.check_bool("window_alive_2", IsWindow(hwnd) != 0);
    ctx.check_int("cb_called_2", g_close_hits);
}

CONF_TEST(gfx_sizes)
{
    install_timer();
    static const int sizes[][2] = { { 640, 480 }, { 320, 240 }, { 256, 224 }, { 322, 240 }, { 960, 720 } };
    for(int i = 0; i < 5; i++)
    {
        int w = sizes[i][0], h = sizes[i][1];
        int r = set_gfx_mode(GFX_AUTODETECT_WINDOWED, w, h, 0, 0);
        ctx.check_int(conf::nm("set_%dx%d", w, h), r);
        if(r == 0)
        {
            place_window(win_get_window());
            ctx.checkf(conf::nm("screen_%dx%d", w, h), "%dx%d d%d SCREEN %dx%d", screen->w, screen->h,
                       bitmap_color_depth(screen), SCREEN_W, SCREEN_H);
            check_window(ctx, conf::nm("window_%dx%d", w, h));
        }
    }
}

CONF_TEST(gfx_virtual_size)
{
    install_timer();
    int r = set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 320, 480);
    ctx.check_int("set_virtual", r);
    if(r == 0)
        ctx.checkf("screen", "%dx%d", screen->w, screen->h);
    else
        ctx.check_bool("screen_after_fail", screen != NULL);
    r = set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    ctx.check_int("set_plain", r);
}

CONF_TEST(gfx_text_mode)
{
    install_timer();
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    HWND hwnd = win_get_window();
    place_window(hwnd);
    int before = _gfx_mode_set_count;
    ctx.check_int("text", set_gfx_mode(GFX_TEXT, 0, 0, 0, 0));
    ctx.check_int("mode_set_count_delta", _gfx_mode_set_count - before);
    ctx.check_bool("screen_null", screen == NULL);
    ctx.check_bool("driver_null", gfx_driver == NULL);
    ctx.check_hex("caps", (unsigned)gfx_capabilities);
    ctx.check_int("SCREEN_W", SCREEN_W);
    ctx.check_bool("hwnd_same", hwnd == win_get_window());
    ctx.check_bool("hwnd_visible", hwnd && IsWindowVisible(hwnd) != 0);
    ctx.check_int("text_again", set_gfx_mode(GFX_TEXT, 80, 25, 0, 0));
    ctx.check_int("reopen", set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0));
    ctx.check_bool("reopen_screen", screen != NULL);
    ctx.check_bool("reopen_hwnd_same", hwnd == win_get_window());
    place_window(win_get_window());
    check_window(ctx, "reopen_window");
}

CONF_TEST(gfx_autodetect)
{
    /* GFX_AUTODETECT after a windowed mode: stays windowed if possible. */
    install_timer();
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    place_window(win_get_window());
    int r = set_gfx_mode(GFX_AUTODETECT, 320, 240, 0, 0);
    ctx.check_int("set", r);
    if(r == 0)
    {
        place_window(win_get_window());
        ctx.check_bool("windowed", is_windowed_mode() != 0);
        ctx.check_hex("driver_id", (unsigned)gfx_driver->id);
    }
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
}

CONF_TEST(gfx_triple_buffer)
{
    install_timer();
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    place_window(win_get_window());
    ctx.check_hex("caps_before", (unsigned)gfx_capabilities);
    ctx.check_int("enable", enable_triple_buffer());
    ctx.check_hex("caps_after", (unsigned)(gfx_capabilities & GFX_CAN_TRIPLE_BUFFER));
    ctx.check_int("poll_scroll", poll_scroll());
    ctx.check_int("request_scroll", request_scroll(0, 0));
    ctx.check_int("scroll_screen_0", scroll_screen(0, 0));
    ctx.check_int("scroll_screen_10", scroll_screen(0, 10));
    ctx.check_int("poll_scroll_after", poll_scroll());
}

CONF_TEST(gfx_video_bitmaps)
{
    install_timer();
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    place_window(win_get_window());
    BITMAP *v[3];
    for(int i = 0; i < 3; i++)
    {
        v[i] = create_video_bitmap(SCREEN_W, SCREEN_H);
        ctx.check_bool(conf::nm("video_%d", i), v[i] != NULL);
        if(v[i])
        {
            ctx.checkf(conf::nm("video_%d_props", i), "%dx%d d%d video %d memory %d screen %d",
                       v[i]->w, v[i]->h, bitmap_color_depth(v[i]), is_video_bitmap(v[i]) != 0,
                       is_memory_bitmap(v[i]) != 0, is_screen_bitmap(v[i]) != 0);
            ctx.check_clip(conf::nm("video_%d_clip", i), v[i]);
        }
    }
    BITMAP *sys = create_system_bitmap(64, 32);
    ctx.check_bool("system", sys != NULL);
    if(sys)
        ctx.checkf("system_props", "%dx%d d%d system %d memory %d", sys->w, sys->h, bitmap_color_depth(sys),
                   is_system_bitmap(sys) != 0, is_memory_bitmap(sys) != 0);

    if(v[0])
    {
        clear_to_color(v[0], 7);
        int r = show_video_bitmap(v[0]);
        ctx.check_int("show_video_bitmap", r);
        ctx.check_int("poll_scroll", poll_scroll());
    }
    if(v[1])
    {
        int r = request_video_bitmap(v[1]);
        ctx.check_int("request_video_bitmap", r);
        plain_wait(100);
        ctx.check_int("poll_scroll_later", poll_scroll());
    }
    if(v[0])
        ctx.check_int("request_screen", request_video_bitmap(screen));
    for(int i = 0; i < 3; i++)
        if(v[i])
            destroy_bitmap(v[i]);
    if(sys)
        destroy_bitmap(sys);
}

CONF_TEST(gfx_vsync_timing)
{
    install_timer();
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    place_window(win_get_window());
    plain_wait(200);
    vsync();
    clk::time_point t0 = clk::now();
    for(int i = 0; i < 30; i++)
        vsync();
    double d = ms_since(t0) / 30.0;
    ctx.note("vsync interval %.3f ms", d);
    /* Windowed: the reference does not wait for the retrace at all. */
    ctx.checkf("vsync_windowed", "%s", d < 2.0 ? "returns at once" : "waits");

    PALETTE pal;
    get_palette(pal);
    t0 = clk::now();
    for(int i = 0; i < 10; i++)
        set_palette(pal);
    double sp = ms_since(t0) / 10.0;
    ctx.note("set_palette %.3f ms", sp);
    t0 = clk::now();
    for(int i = 0; i < 10; i++)
        set_palette_range(pal, 0, 255, FALSE);
    double spr = ms_since(t0) / 10.0;
    ctx.note("set_palette_range(no vsync) %.3f ms", spr);
    ctx.checkf("set_palette_waits", "%s", sp >= 4.0 ? "yes" : "no");
}

CONF_TEST(gfx_color_depths)
{
    install_timer();
    static const int depths[] = { 8, 15, 16, 24, 32 };
    for(int i = 0; i < 5; i++)
    {
        set_color_depth(depths[i]);
        int r = set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
        ctx.check_int(conf::nm("set_%d", depths[i]), r);
        if(r == 0)
        {
            place_window(win_get_window());
            ctx.checkf(conf::nm("screen_%d", depths[i]), "%dx%d d%d", screen->w, screen->h, bitmap_color_depth(screen));
            ctx.check_hash(conf::nm("cleared_%d", depths[i]), hash_screen());
            BITMAP *vb = create_video_bitmap(32, 32);
            ctx.checkf(conf::nm("video_%d", depths[i]), "%d", vb ? bitmap_color_depth(vb) : -1);
            if(vb)
                destroy_bitmap(vb);
        }
    }
    set_color_depth(8);
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
}

CONF_TEST(gfx_screen_drawing)
{
    install_timer();
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    place_window(win_get_window());
    for(int i = 0; i < 2000; i++)
        putpixel(screen, ctx.rng.range(0, 319), ctx.rng.range(0, 239), ctx.rng.range(0, 255));
    ctx.check_hash("drawn", hash_screen());
    vsync();
    ctx.check_hash("after_vsync", hash_screen());
    /* set_gfx_mode again clears. */
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    place_window(win_get_window());
    ctx.check_hash("reset", hash_screen());
}

CONF_TEST(gfx_desktop)
{
    install_timer();
    int w = 0, h = 0;
    int r = get_desktop_resolution(&w, &h);
    ctx.check_int("get_desktop_resolution", r);
    ctx.check_bool("same_as_win32", w == GetSystemMetrics(SM_CXSCREEN) && h == GetSystemMetrics(SM_CYSCREEN));
    ctx.check_int("desktop_color_depth", desktop_color_depth());
}

CONF_TEST(gfx_mouse_after_mode)
{
    /* The reference's windowed drivers use a hardware cursor while the
     * pointer is shown on the screen, and say so in gfx_capabilities. */
    install_timer();
    install_mouse();
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    place_window(win_get_window());
    ctx.check_hex("caps_no_mouse", (unsigned)gfx_capabilities);
    show_mouse(screen);
    ctx.check_hex("caps_show_mouse", (unsigned)gfx_capabilities);
    plain_wait(50);
    ctx.check_hash("screen_with_mouse", hash_screen());
    scare_mouse();
    ctx.check_hex("caps_scared", (unsigned)gfx_capabilities);
    unscare_mouse();
    ctx.check_hex("caps_unscared", (unsigned)gfx_capabilities);
    BITMAP *other = create_bitmap(64, 64);
    show_mouse(other);
    ctx.check_hex("caps_other_bitmap", (unsigned)gfx_capabilities);
    show_mouse(NULL);
    ctx.check_hex("caps_hidden", (unsigned)gfx_capabilities);
    destroy_bitmap(other);
    show_mouse(screen);
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    place_window(win_get_window());
    ctx.check_hex("caps_after_new_mode", (unsigned)gfx_capabilities);
    set_gfx_mode(GFX_GDI, 320, 240, 0, 0);
    place_window(win_get_window());
    show_mouse(screen);
    ctx.check_hex("caps_gdi_show_mouse", (unsigned)gfx_capabilities);
    show_mouse(NULL);
    remove_mouse();
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
}

namespace
{
    /* What the window shows: the client area copied from the desktop DC
     * (after moving the window on top), as 0xRRGGBB values. */
    bool capture_client(std::vector<unsigned> *out, int *w, int *h)
    {
        HWND hwnd = win_get_window();
        if(!hwnd)
            return false;
        RECT rc;
        GetClientRect(hwnd, &rc);
        POINT p0 = { 0, 0 };
        ClientToScreen(hwnd, &p0);
        int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
        if(cw <= 0 || ch <= 0)
            return false;
        HDC sdc = GetDC(NULL);
        HDC mdc = CreateCompatibleDC(sdc);
        BITMAPINFO bi;
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = cw;
        bi.bmiHeader.biHeight = -ch;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void *bits = NULL;
        HBITMAP dib = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        HGDIOBJ old = SelectObject(mdc, dib);
        BitBlt(mdc, 0, 0, cw, ch, sdc, p0.x, p0.y, SRCCOPY);
        GdiFlush();
        out->resize((size_t)cw * ch);
        const unsigned *src = (const unsigned *)bits;
        for(int i = 0; i < cw * ch; i++)
            (*out)[i] = src[i] & 0xFFFFFF;
        SelectObject(mdc, old);
        DeleteObject(dib);
        DeleteDC(mdc);
        ReleaseDC(NULL, sdc);
        *w = cw;
        *h = ch;
        return true;
    }

    void bring_to_top()
    {
        HWND hwnd = win_get_window();
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        /* keep the OS pointer away from the window */
        RECT wr;
        GetWindowRect(hwnd, &wr);
        SetCursorPos(wr.right + 40, wr.bottom + 40);
    }

    void release_top()
    {
        SetWindowPos(win_get_window(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    /* Lets the display catch up: rest() (presents on the port) and time for
     * the compositor. */
    void settle()
    {
        for(int i = 0; i < 20; i++)
            rest(10);
    }

    void make_test_palette(PALETTE pal, int variant)
    {
        for(int i = 0; i < 256; i++)
        {
            pal[i].r = (unsigned char)((i + variant * 7) & 63);
            pal[i].g = (unsigned char)(((i >> 2) * 3 + variant) & 63);
            pal[i].b = (unsigned char)(63 - (i & 63));
        }
    }

    void draw_grid(BITMAP *bmp)
    {
        int cw = bmp->w / 16, ch = bmp->h / 16;
        for(int i = 0; i < 256; i++)
        {
            int x0 = (i & 15) * cw, y0 = (i >> 4) * ch;
            for(int y = y0; y < y0 + ch; y++)
                for(int x = x0; x < x0 + cw; x++)
                    putpixel(bmp, x, y, i);
        }
    }

    void check_grid_capture(conf::Ctx &ctx, const char *c, int sw, int sh)
    {
        std::vector<unsigned> px;
        int w = 0, h = 0;
        if(!capture_client(&px, &w, &h))
        {
            ctx.fail(c, "capture failed");
            return;
        }
        if(w != sw || h != sh)
        {
            ctx.fail(c, "client %dx%d", w, h);
            return;
        }
        int cw = sw / 16, ch = sh / 16;
        conf::Seq s(ctx, c);
        for(int i = 0; i < 256; i++)
        {
            int x = (i & 15) * cw + cw / 2, y = (i >> 4) * ch + ch / 2;
            s.add_hex(px[(size_t)y * w + x], "index %d", i);
        }
        s.end();
    }
}

CONF_TEST(gfx_capture_8bit)
{
    install_timer();
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    place_window(win_get_window());
    bring_to_top();
    PALETTE pal;
    make_test_palette(pal, 0);
    set_palette(pal);
    draw_grid(screen);
    settle();
    check_grid_capture(ctx, "grid", 320, 240);

    /* Palette change alone updates what is shown. */
    make_test_palette(pal, 1);
    set_palette(pal);
    settle();
    check_grid_capture(ctx, "grid_pal2", 320, 240);

    /* All 64 levels of each component. */
    for(int i = 0; i < 256; i++)
    {
        pal[i].r = (unsigned char)(i & 63);
        pal[i].g = (unsigned char)((i + 21) & 63);
        pal[i].b = (unsigned char)((i * 5) & 63);
    }
    set_palette(pal);
    settle();
    check_grid_capture(ctx, "levels", 320, 240);
    release_top();
    set_palette(default_palette);
}

CONF_TEST(gfx_capture_show_video_bitmap)
{
    install_timer();
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    place_window(win_get_window());
    bring_to_top();
    PALETTE pal;
    make_test_palette(pal, 2);
    set_palette(pal);
    clear_to_color(screen, 3);
    BITMAP *v = create_video_bitmap(320, 240);
    if(!v)
    {
        ctx.fail("video", "NULL");
        release_top();
        return;
    }
    draw_grid(v);
    settle();
    check_grid_capture(ctx, "before", 320, 240);
    ctx.check_int("show", show_video_bitmap(v));
    settle();
    check_grid_capture(ctx, "after_show", 320, 240);
    ctx.check_int("screen_pixel", getpixel(screen, 5, 5));
    clear_to_color(screen, 9);
    settle();
    check_grid_capture(ctx, "after_screen_clear", 320, 240);
    /* The first screen-sized video bitmap shares the screen's memory.
     * (What the reference's window shows right after drawing through 'v'
     * depends on DirectDraw's update of the window, which only follows
     * drawing through 'screen'; so the memory is checked here, and the
     * window again after drawing on 'screen'.) */
    clear_to_color(v, 12);
    ctx.check_int("screen_after_video_clear", getpixel(screen, 5, 5));
    putpixel(screen, 0, 0, 12);
    settle();
    check_grid_capture(ctx, "after_video_clear", 320, 240);
    ctx.check_int("show_screen", show_video_bitmap(screen));
    ctx.check_int("screen_after_show_screen", getpixel(screen, 5, 5));
    destroy_bitmap(v);
    release_top();
    set_palette(default_palette);
}

CONF_TEST(gfx_capture_hicolor)
{
    /* 15, 16 and 24-bit screens: every value of each component. */
    install_timer();
    static const int depths[] = { 15, 16, 24 };
    for(int d = 0; d < 3; d++)
    {
        set_color_depth(depths[d]);
        int r = set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
        ctx.check_int(conf::nm("set_%d", depths[d]), r);
        if(r != 0)
            continue;
        set_display_switch_mode(SWITCH_BACKGROUND);
        place_window(win_get_window());
        bring_to_top();
        for(int i = 0; i < 256; i++)
        {
            int col;
            int rr = (i * 8) & 255, gg = (i * 4) & 255, bb = 255 - ((i * 8) & 255);
            if(depths[d] == 15)
                col = ((rr >> 3) << 10) | ((gg >> 3) << 5) | (bb >> 3);
            else if(depths[d] == 16)
                col = ((rr >> 3) << 11) | ((gg >> 2) << 5) | (bb >> 3);
            else
                col = (i << 16) | ((i * 3 & 255) << 8) | (255 - i);
            int x0 = (i & 15) * 20, y0 = (i >> 4) * 15;
            for(int y = y0; y < y0 + 15; y++)
                for(int x = x0; x < x0 + 20; x++)
                    putpixel(screen, x, y, col);
        }
        settle();
        check_grid_capture(ctx, conf::nm("grid_%d", depths[d]), 320, 240);
        release_top();
    }
    set_color_depth(8);
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
}

CONF_TEST(gfx_failures)
{
    install_timer();
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    HWND h0 = win_get_window();
    int before = _gfx_mode_set_count;
    ctx.check_int("odd_width", set_gfx_mode(GFX_AUTODETECT_WINDOWED, 322, 240, 0, 0));
    ctx.check_str("odd_width_error", allegro_error);
    ctx.check_int("count_delta", _gfx_mode_set_count - before);
    ctx.check_bool("screen_after_fail", screen != NULL);
    ctx.check_bool("driver_after_fail", gfx_driver != NULL);
    ctx.check_bool("window_kept", h0 != NULL && h0 == win_get_window() && IsWindowVisible(h0) != 0);
    ctx.check_int("virtual_bigger", set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 640, 480));
    ctx.check_str("virtual_error", allegro_error);
    ctx.check_int("virtual_same", set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 320, 240));
    ctx.check_int("virtual_smaller", set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 100, 100));
    static const int w[] = { 4, 64, 100, 116, 120, 128, 324 };
    for(int i = 0; i < 7; i++)
        ctx.check_int(conf::nm("width_%d", w[i]), set_gfx_mode(GFX_AUTODETECT_WINDOWED, w[i], 100, 0, 0));
    ctx.check_int("height_1", set_gfx_mode(GFX_AUTODETECT_WINDOWED, 256, 1, 0, 0));
    ctx.check_int("bogus_card", set_gfx_mode(12345, 320, 240, 0, 0));
    ctx.check_str("bogus_error", allegro_error);
    ctx.check_int("safe", set_gfx_mode(GFX_SAFE, 320, 240, 0, 0));
    if(gfx_driver)
        ctx.checkf("safe_driver", "%dx%d d%d id %08x windowed %d caps %08x", screen->w, screen->h,
                   bitmap_color_depth(screen), gfx_driver->id, gfx_driver->windowed, gfx_capabilities);
    ctx.check_int("gdi", set_gfx_mode(GFX_GDI, 320, 240, 0, 0));
    if(gfx_driver)
        ctx.checkf("gdi_driver", "id %08x caps %08x", gfx_driver->id, gfx_capabilities);
    ctx.check_int("dxwin", set_gfx_mode(GFX_DIRECTX_WIN, 320, 240, 0, 0));
    if(gfx_driver)
        ctx.checkf("dxwin_driver", "id %08x caps %08x", gfx_driver->id, gfx_capabilities);
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
}

CONF_TEST(gfx_mode_queries)
{
    install_timer();
    ctx.check_int("refresh_no_mode", get_refresh_rate());
    ctx.check_int("type_win", get_gfx_mode_type(GFX_AUTODETECT_WINDOWED));
    ctx.check_int("type_full", get_gfx_mode_type(GFX_AUTODETECT_FULLSCREEN));
    ctx.check_int("type_auto", get_gfx_mode_type(GFX_AUTODETECT));
    ctx.check_int("type_safe", get_gfx_mode_type(GFX_SAFE));
    ctx.check_int("type_text", get_gfx_mode_type(GFX_TEXT));
    ctx.check_int("type_dxwin", get_gfx_mode_type(GFX_DIRECTX_WIN));
    ctx.check_int("type_dx", get_gfx_mode_type(GFX_DIRECTX));
    ctx.check_int("type_gdi", get_gfx_mode_type(GFX_GDI));
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    ctx.check_hex("gfx_mode", (unsigned)get_gfx_mode());
    int r0 = get_refresh_rate();
    ctx.check_bool("refresh_known", r0 > 0);
    request_refresh_rate(r0 == 75 ? 85 : 75);
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    ctx.check_bool("refresh_ignores_request", get_refresh_rate() == r0);
    request_refresh_rate(0);
}

static void sw_c0() {} static void sw_c1() {} static void sw_c2() {} static void sw_c3() {}
static void sw_c4() {} static void sw_c5() {} static void sw_c6() {} static void sw_c7() {}
static void sw_c8() {}

CONF_TEST(gfx_switch_mode)
{
    install_timer();
    set_gfx_mode(GFX_TEXT, 0, 0, 0, 0);
    ctx.check_int("mode_before_gfx", get_display_switch_mode());
    void (*cbs[9])() = { sw_c0, sw_c1, sw_c2, sw_c3, sw_c4, sw_c5, sw_c6, sw_c7, sw_c8 };
    int n = 0;
    for(int i = 0; i < 9; i++)
        if(set_display_switch_callback(SWITCH_IN, cbs[i]) == 0)
            n++;
    ctx.check_int("registered", n);
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    ctx.check_int("mode_after_gfx", get_display_switch_mode());
    ctx.check_int("callbacks_cleared", set_display_switch_callback(SWITCH_IN, sw_c8));
    place_window(win_get_window());
    ctx.check_int("set_background", set_display_switch_mode(SWITCH_BACKGROUND));
    ctx.check_int("set_pause", set_display_switch_mode(SWITCH_PAUSE));
    ctx.check_int("set_background_2", set_display_switch_mode(SWITCH_BACKGROUND));
    ctx.check_int("mode_now", get_display_switch_mode());
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 640, 480, 0, 0);
    ctx.check_int("mode_after_gfx_2", get_display_switch_mode());
    place_window(win_get_window());
    set_gfx_mode(GFX_TEXT, 0, 0, 0, 0);
    ctx.check_int("mode_after_text", get_display_switch_mode());
    set_display_switch_mode(SWITCH_BACKGROUND);
}

CONF_TEST(gfx_mode_resets)
{
    install_timer();
    install_mouse();
    PALETTE pal, got;
    for(int i = 0; i < 256; i++)
    {
        pal[i].r = (unsigned char)(i & 63);
        pal[i].g = 5;
        pal[i].b = 7;
        pal[i].filler = 0;
    }
    set_palette(pal);
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    set_display_switch_mode(SWITCH_BACKGROUND);
    place_window(win_get_window());
    get_palette(got);
    ctx.check_bool("palette_is_default", conf::hash_palette(got) == conf::hash_palette(default_palette));
    set_palette(pal);
    show_mouse(screen);
    position_mouse(100, 100);
    set_mouse_range(10, 10, 50, 50);
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 640, 480, 0, 0);
    set_display_switch_mode(SWITCH_BACKGROUND);
    place_window(win_get_window());
    get_palette(got);
    ctx.check_bool("palette_is_default_2", conf::hash_palette(got) == conf::hash_palette(default_palette));
    ctx.check_bool("mouse_hidden", _mouse_screen == NULL);
    ctx.checkf("mouse_pos", "%d %d", mouse_x, mouse_y);
    position_mouse(600, 400);
    ctx.checkf("mouse_range_reset", "%d %d", mouse_x, mouse_y);
    remove_mouse();
}

CONF_TEST(gfx_capture_32bit)
{
    install_timer();
    set_color_depth(32);
    int r = set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
    ctx.check_int("set", r);
    if(r != 0)
    {
        set_color_depth(8);
        return;
    }
    place_window(win_get_window());
    bring_to_top();
    int cw = 20, ch = 15;
    for(int i = 0; i < 256; i++)
    {
        int col = makecol32((i * 37) & 255, (i * 91 + 13) & 255, (255 - i));
        int x0 = (i & 15) * cw, y0 = (i >> 4) * ch;
        for(int y = y0; y < y0 + ch; y++)
            for(int x = x0; x < x0 + cw; x++)
                putpixel(screen, x, y, col);
    }
    settle();
    check_grid_capture(ctx, "grid", 320, 240);
    release_top();
    set_color_depth(8);
    set_gfx_mode(GFX_AUTODETECT_WINDOWED, 320, 240, 0, 0);
}

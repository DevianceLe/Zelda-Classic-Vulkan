/*
 * test_platform_timer_sdl.cpp - timers and rest() (platform module).
 *
 * Timing depends on the machine, so every timed check is a tolerance range
 * printed as ok/FAIL (the same on both sides when both are within range);
 * the measured values only go to notes. The ranges are chosen around what
 * the reference build does (see the notes of a --ref-only run).
 *
 * The time base is std::chrono::steady_clock on both sides, never Allegro.
 */
#include "conf.h"

#include <chrono>
#include <thread>

namespace
{
    typedef std::chrono::steady_clock clk;

    double ms_since(clk::time_point t0)
    {
        return std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    }

    /* Waits 'ms' without any Allegro call. */
    void plain_wait(double ms)
    {
        clk::time_point t0 = clk::now();
        while(ms_since(t0) < ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    volatile int g_count_a = 0;
    volatile int g_count_b = 0;
    volatile int g_rest_cb = 0;
    volatile int g_param_hits[32];
    volatile int g_other_thread = 0;
    std::thread::id g_main_thread;

    void tick_a() { g_count_a = g_count_a + 1; }
    END_OF_FUNCTION(tick_a)
    void tick_b() { g_count_b = g_count_b + 1; }
    END_OF_FUNCTION(tick_b)
    void rest_cb() { g_rest_cb = g_rest_cb + 1; }
    void tick_thread()
    {
        if(std::this_thread::get_id() != g_main_thread)
            g_other_thread = 1;
    }
    void tick_param(void *p)
    {
        int i = (int)(size_t)p;
        g_param_hits[i] = g_param_hits[i] + 1;
    }

    /* Counts ticks of tick_a over 'ms' milliseconds. */
    int count_over(double ms)
    {
        int c0 = g_count_a;
        plain_wait(ms);
        return g_count_a - c0;
    }
}

CONF_TEST(timer_install)
{
    ctx.check_int("install", install_timer());
    ctx.check_int("install_again", install_timer());
}

CONF_TEST(timer_rate_60hz)
{
    install_timer();
    g_count_a = 0;
    ctx.check_int("install_int_ex", install_int_ex(tick_a, BPS_TO_TIMER(60)));
    plain_wait(100);                       /* settle */
    int n = count_over(2000);
    ctx.note("60 Hz timer: %d ticks in 2000 ms", n);
    ctx.expect("rate", n >= 116 && n <= 124, "%d ticks in 2 s", n);
    remove_int(tick_a);
}

CONF_TEST(timer_rate_install_int_ms)
{
    install_timer();
    g_count_a = 0;
    ctx.check_int("install_int", install_int(tick_a, 25));
    plain_wait(60);
    int n = count_over(1000);
    ctx.note("install_int(25 ms): %d ticks in 1000 ms", n);
    ctx.expect("rate", n >= 38 && n <= 42, "%d ticks in 1 s", n);
    remove_int(tick_a);
}

CONF_TEST(timer_rate_1s)
{
    install_timer();
    g_count_a = 0;
    ctx.check_int("install_int_ex", install_int_ex(tick_a, SECS_TO_TIMER(1)));
    int n = count_over(2500);
    ctx.note("1 s timer: %d ticks in 2500 ms", n);
    ctx.expect("rate", n == 2, "%d ticks in 2.5 s", n);
    remove_int(tick_a);
}

CONF_TEST(timer_speed_change)
{
    install_timer();
    g_count_a = 0;
    ctx.check_int("install_60", install_int_ex(tick_a, BPS_TO_TIMER(60)));
    plain_wait(100);
    ctx.check_int("reinstall_120", install_int_ex(tick_a, BPS_TO_TIMER(120)));
    plain_wait(50);
    int n = count_over(1000);
    ctx.note("after change to 120 Hz: %d ticks in 1000 ms", n);
    ctx.expect("rate", n >= 114 && n <= 126, "%d ticks in 1 s", n);
    remove_int(tick_a);
}

CONF_TEST(timer_two_timers)
{
    install_timer();
    g_count_a = 0;
    g_count_b = 0;
    ctx.check_int("install_a", install_int_ex(tick_a, BPS_TO_TIMER(60)));
    ctx.check_int("install_b", install_int(tick_b, 100));
    plain_wait(1000);
    int a = g_count_a, b = g_count_b;
    ctx.note("two timers: a=%d b=%d in 1000 ms", a, b);
    ctx.expect("rate_a", a >= 56 && a <= 64, "a=%d", a);
    ctx.expect("rate_b", b >= 9 && b <= 11, "b=%d", b);
    remove_int(tick_a);
    remove_int(tick_b);
}

CONF_TEST(timer_remove)
{
    install_timer();
    g_count_a = 0;
    install_int_ex(tick_a, BPS_TO_TIMER(200));
    plain_wait(100);
    remove_int(tick_a);
    plain_wait(20);
    int n0 = g_count_a;
    plain_wait(200);
    ctx.expect("ticked", n0 > 0, "%d", n0);
    ctx.expect("stopped", g_count_a == n0, "%d -> %d", n0, (int)g_count_a);
    remove_int(tick_a);                    /* not installed: harmless */
    remove_int(tick_b);
    ctx.expect("still", g_count_a == n0, "%d", (int)g_count_a);
}

CONF_TEST(timer_runs_on_other_thread)
{
    install_timer();
    g_main_thread = std::this_thread::get_id();
    g_other_thread = 0;
    install_int(tick_thread, 5);
    plain_wait(100);
    remove_int(tick_thread);
    ctx.check_bool("other_thread", g_other_thread != 0);
}

CONF_TEST(timer_slots)
{
    install_timer();
    for(int i = 0; i < 32; ++i)
        g_param_hits[i] = 0;

    int ok = 0;
    for(int i = 0; i < 32; ++i)
    {
        if(install_param_int(tick_param, (void *)(size_t)i, 1000) != 0)
            break;
        ++ok;
    }
    ctx.check_int("param_slots", ok);

    /* Same proc and param again: adjusts the speed, no new slot. */
    if(ok > 0)
        ctx.check_int("reinstall_same", install_param_int(tick_param, (void *)(size_t)0, 500));

    ctx.check_int("plain_when_full", install_int(tick_a, 1000));
    remove_int(tick_a);

    for(int i = 0; i < 32; ++i)
        remove_param_int(tick_param, (void *)(size_t)i);

    ctx.check_int("after_remove", install_int(tick_a, 1000));
    remove_int(tick_a);
}

CONF_TEST(timer_param_values)
{
    install_timer();
    for(int i = 0; i < 32; ++i)
        g_param_hits[i] = 0;
    install_param_int_ex(tick_param, (void *)(size_t)3, BPS_TO_TIMER(100));
    install_param_int_ex(tick_param, (void *)(size_t)5, BPS_TO_TIMER(50));
    plain_wait(1000);
    remove_param_int(tick_param, (void *)(size_t)3);
    remove_param_int(tick_param, (void *)(size_t)5);
    int a = g_param_hits[3], b = g_param_hits[5], other = 0;
    for(int i = 0; i < 32; ++i)
        if(i != 3 && i != 5)
            other += g_param_hits[i];
    ctx.note("param timers: 3 -> %d, 5 -> %d", a, b);
    ctx.expect("rate_3", a >= 94 && a <= 106, "%d", a);
    ctx.expect("rate_5", b >= 47 && b <= 53, "%d", b);
    ctx.check_int("others", other);
}

CONF_TEST(timer_auto_install)
{
    /* install_int() without install_timer() installs the timer module. */
    remove_timer();
    g_count_a = 0;
    ctx.check_int("install_int", install_int(tick_a, 10));
    plain_wait(300);
    remove_int(tick_a);
    ctx.expect("ticked", g_count_a >= 20, "%d", (int)g_count_a);
    ctx.check_int("install_timer", install_timer());
}

CONF_TEST(timer_rest_durations)
{
    install_timer();
    static const unsigned waits[] = { 2, 10, 30, 100 };
    for(int i = 0; i < 4; ++i)
    {
        unsigned w = waits[i];
        clk::time_point t0 = clk::now();
        rest(w);
        double d = ms_since(t0);
        ctx.note("rest(%u): %.2f ms", w, d);
        ctx.expect(conf::nm("rest_%u", w), d >= w - 0.5 && d <= w + 20, "%.2f ms", d);
    }

    clk::time_point t0 = clk::now();
    for(int i = 0; i < 100; ++i)
        rest(0);
    double d = ms_since(t0);
    ctx.note("100 x rest(0): %.2f ms", d);
    ctx.expect("rest_0", d < 50, "%.2f ms", d);

    t0 = clk::now();
    for(int i = 0; i < 20; ++i)
        rest(1);
    d = ms_since(t0);
    ctx.note("20 x rest(1): %.2f ms", d);
    ctx.expect("rest_1", d >= 19 && d <= 400, "%.2f ms", d);
}

CONF_TEST(timer_rest_callback)
{
    install_timer();
    g_rest_cb = 0;
    clk::time_point t0 = clk::now();
    rest_callback(60, rest_cb);
    double d = ms_since(t0);
    int n = g_rest_cb;
    ctx.note("rest_callback(60): %.2f ms, %d calls", d, n);
    ctx.expect("duration", d >= 59.5 && d <= 90, "%.2f ms", d);
    ctx.expect("called", n >= 2, "%d", n);

    g_rest_cb = 0;
    t0 = clk::now();
    rest_callback(30, NULL);               /* same as rest(30) */
    d = ms_since(t0);
    ctx.expect("null_cb", d >= 29.5 && d <= 60, "%.2f ms", d);

    g_rest_cb = 0;
    rest_callback(0, rest_cb);
    ctx.note("rest_callback(0): %d calls", (int)g_rest_cb);
    ctx.check_bool("zero_calls_cb", g_rest_cb != 0);
}

CONF_TEST(timer_retrace_count)
{
    install_timer();
    int r0 = retrace_count;
    plain_wait(1000);
    int n = retrace_count - r0;
    ctx.note("retrace_count: +%d in 1000 ms", n);
    ctx.expect("rate", n >= 66 && n <= 74, "%d", n);
}

CONF_TEST(timer_tiny_speed)
{
    /* zelda.cpp installs install_int_ex(update_script_counter, 1) when
     * _SCRIPT_COUNTER is defined: speeds below the minimum are clamped. */
    install_timer();
    g_count_a = 0;
    ctx.check_int("install", install_int_ex(tick_a, 1));
    plain_wait(20);
    int n = count_over(1000);
    remove_int(tick_a);
    ctx.note("install_int_ex(proc, 1): %d ticks in 1000 ms", n);
    ctx.checkf("rate_class", "%s", n < 500 ? "<500" : n < 2000 ? "500..2000" : n < 20000 ? "2000..20000" : ">=20000");
}

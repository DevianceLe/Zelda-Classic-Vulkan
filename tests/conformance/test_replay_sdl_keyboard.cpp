/*
 * test_replay_sdl_keyboard.cpp - the keyboard-buffer behaviour the replay
 * (src/replay.cpp, docs/zplayer-replay.md) relies on: keyboard_callback
 * filtering, simulate_keypress(), clear_keybuf(), and the keypressed/readkey
 * hooks of install_keyboard_hooks().
 *
 * An _sdl test: Allegro needs its system driver here (keypressed() with
 * hooks and simulate_keypress() crash under SYSTEM_NONE), so both sides run
 * with SYSTEM_AUTODETECT and the port side links zcport_sdl.
 */
#include "conf.h"

namespace
{
    int g_fake[8];
    int g_fake_n = 0;
    int g_kp_calls = 0;
    int g_rk_calls = 0;

    int fake_keypressed(void)
    {
        ++g_kp_calls;
        return g_fake_n > 0;
    }

    int fake_readkey(void)
    {
        ++g_rk_calls;
        if(g_fake_n <= 0)
            return 0;
        int k = g_fake[0];
        for(int i = 1; i < g_fake_n; i++)
            g_fake[i-1] = g_fake[i];
        --g_fake_n;
        return k;
    }

    int g_cb_calls = 0;
    int g_cb_last = 0;
    int g_cb_result = 0;

    int callback(int k)
    {
        ++g_cb_calls;
        g_cb_last = k;
        return g_cb_result < 0 ? k : g_cb_result;
    }

    void drain()
    {
        while(keypressed())
            readkey();
    }
}

CONF_TEST(replay_keyboard_hooks)
{
    g_fake_n = 3;
    g_fake[0] = (KEY_L << 8) | 'l';
    g_fake[1] = (KEY_I << 8) | 'i';
    g_fake[2] = (KEY_ENTER << 8) | 13;
    g_kp_calls = g_rk_calls = 0;
    install_keyboard_hooks(fake_keypressed, fake_readkey);

    ctx.check_bool("keypressed", keypressed() != 0);
    ctx.check_hex("readkey", readkey());
    ctx.check_int("left_after_readkey", g_fake_n);

    // Does clear_keybuf() empty the hooked buffer (through the hooks)?
    clear_keybuf();
    ctx.check_int("left_after_clear_keybuf", g_fake_n);
    ctx.check_bool("keypressed_after_clear", keypressed() != 0);

    g_fake_n = 1;
    g_fake[0] = (KEY_K << 8) | 'k';
    int sc = -1;
    int u = ureadkey(&sc);
    ctx.check_hex("ureadkey", u);
    ctx.check_int("ureadkey_scancode", sc);

    install_keyboard_hooks(NULL, NULL);
    g_fake_n = 0;
}

CONF_TEST(replay_keyboard_simulate)
{
    // The game installs the keyboard before the replay starts.
    int installed = install_keyboard();
    ctx.check_int("install_keyboard", installed);
    if(installed != 0)
        return;

    clear_keybuf();
    simulate_keypress((KEY_N << 8) | 'n');
    ctx.check_bool("keypressed", keypressed() != 0);
    ctx.check_hex("readkey", keypressed() ? readkey() : -1);
    drain();

    // keyboard_callback: called for simulated keys? 0 drops the key?
    keyboard_callback = callback;
    g_cb_calls = 0;
    g_cb_last = 0;
    g_cb_result = -1;   // pass the key on unchanged
    simulate_keypress((KEY_I << 8) | 'i');
    ctx.check_int("callback_calls", g_cb_calls);
    ctx.check_hex("callback_value", g_cb_last);
    ctx.check_bool("kept_keypressed", keypressed() != 0);
    drain();

    g_cb_calls = 0;
    g_cb_result = 0;    // drop
    simulate_keypress((KEY_K << 8) | 'k');
    ctx.check_int("drop_callback_calls", g_cb_calls);
    ctx.check_bool("dropped_keypressed", keypressed() != 0);
    drain();

    // clear_keybuf() empties what simulate_keypress() put in.
    simulate_keypress((KEY_L << 8) | 'l');
    simulate_keypress((KEY_I << 8) | 'i');
    clear_keybuf();
    ctx.check_bool("keypressed_after_clear", keypressed() != 0);

    keyboard_callback = NULL;
    clear_keybuf();
    remove_keyboard();
}

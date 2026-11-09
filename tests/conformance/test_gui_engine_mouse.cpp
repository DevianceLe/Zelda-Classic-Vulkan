/*
 * test_gui_engine_mouse.cpp - mouse handling of the dialog manager: button
 * transitions, the wheel, leaving all objects, double click range, what
 * pending results do to the double click wait, and the return values of the
 * mouse messages.
 *
 * Steps that depend on the double click timer (in the reference it runs in
 * its own thread) are separated by pauses, see gui_conf.h.
 */
#include "gui_conf.h"

using namespace guiconf;

namespace
{

struct Row
{
    Obj o[8];
    DIALOG dlg[9];
    int n;

    explicit Row(int count, int want_focus_mask = 0xFE)
    {
        n = count;
        for(int i = 0; i < n; ++i)
        {
            o[i].id = i;
            if(want_focus_mask & (1 << i))
                o[i].ret[MSG_WANTFOCUS] = D_WANTFOCUS;
            if(i == 0)
                dlg[i] = obj(&o[i], 0, 0, 320, 240);
            else
                dlg[i] = obj(&o[i], 10 + (i - 1) * 40, 10, 30, 20);
        }
        dlg[n] = end_obj();
    }
};

}

CONF_TEST(gui_button_transitions)
{
    Session s;
    static const int seq[][2] = { { 1, 2 }, { 2, 1 }, { 3, 4 }, { 4, 3 }, { 7, 0 }, { 5, 6 }, { 6, 1 } };
    for(int k = 0; k < (int)(sizeof(seq) / sizeof(seq[0])); ++k)
    {
        Row r(4);
        log_add(conf::nm("buttons %d -> %d", seq[k][0], seq[k][1]));
        s.mouse(55, 15);
        Run run(r.dlg, -1);
        run.upds(2);
        s.button(seq[k][0]);
        run.upd();
        s.button(seq[k][1]);
        sleep_ms(60);
        run.upd();
        s.button(0);
        sleep_ms(60);
        run.upd();
        run.settle();
        run.upd();
        run.fin();
    }
    /* button changes off every object */
    {
        Row r(4);
        log_add("off objects");
        s.mouse(-5, -5);
        Run run(r.dlg, 1);
        run.upd();
        s.button(1);
        run.upd();
        s.button(0);
        run.upd();
        run.upd();
        run.fin();
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_wheel_order)
{
    Session s;
    for(int v = 0; v < 8; ++v)
    {
        Row r(4);
        log_add(conf::nm("variant %d", v));
        gui_mouse_focus = (v & 4) ? FALSE : TRUE;
        s.mouse((v & 1) ? -5 : 55, 15);
        s.wheel(0);
        Run run(r.dlg, 1);
        run.upd();
        /* wheel together with a key */
        s.wheel(1);
        s.key(KEY_X, 'x');
        run.upd();
        /* wheel together with a click */
        s.wheel(3);
        s.button((v & 2) ? 1 : 0);
        run.upd();
        s.button(0);
        sleep_ms(60);
        run.upd();
        run.settle();
        /* wheel where the idle (no object) or the wheel message closes */
        r.o[2].ret_once[MSG_WHEEL] = D_CLOSE | D_REDRAW;
        r.o[1].ret_once[MSG_IDLE] = D_CLOSE;
        s.wheel(-4);
        run.upd();
        run.upd();
        run.fin();
    }
    s.wheel(0);
    check_log(ctx, "log");
}

CONF_TEST(gui_mouse_leaves)
{
    Session s;
    for(int v = 0; v < 4; ++v)
    {
        Row r(4);
        if(v & 1)
            r.o[2].ret[MSG_LOSTFOCUS] = D_WANTFOCUS;
        gui_mouse_focus = (v & 2) ? FALSE : TRUE;
        log_add(conf::nm("variant %d", v));
        s.mouse(55, 15);
        Run run(r.dlg, 2);
        run.upd();
        s.mouse(-5, 15);       /* outside every object */
        run.upd();
        s.mouse(5, 200);       /* background */
        run.upd();
        s.mouse(55, 15);
        run.upd();
        s.mouse(500, 500);
        run.upd();
        run.fin();
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_mouse_message_returns)
{
    Session s;
    static const int rets[] = { D_CLOSE, D_REDRAW, D_REDRAWME, D_WANTFOCUS, D_USED_CHAR, 0x100 };
    static const int msgs[] = { MSG_GOTMOUSE, MSG_LOSTMOUSE, MSG_WANTMOUSE };
    for(int m = 0; m < 3; ++m)
    {
        for(int i = 0; i < (int)(sizeof(rets) / sizeof(rets[0])); ++i)
        {
            Row r(4);
            r.o[2].ret_once[msgs[m]] = rets[i];
            log_add(conf::nm("%s returns 0x%x", msg_name(msgs[m]), rets[i]));
            s.mouse(5, 200);
            Run run(r.dlg, 1);
            run.upd();
            s.mouse(55, 15);
            run.upd();
            s.mouse(95, 15);
            run.upd();
            run.upd();
            run.fin();
        }
    }
    check_log(ctx, "log");
}

/* bits left in the result by earlier messages and the double click wait */
CONF_TEST(gui_click_wait_pending)
{
    Session s;
    static const int rets[] = { 0, D_USED_CHAR, D_DONTWANTMOUSE, 0x80, 0x100, 0x4000, D_REDRAWME };
    for(int i = 0; i < (int)(sizeof(rets) / sizeof(rets[0])); ++i)
    {
        Row r(4);
        r.o[3].ret_once[MSG_IDLE] = rets[i];
        log_add(conf::nm("idle left 0x%x", rets[i]));
        s.mouse(55, 15);
        Run run(r.dlg, -1);
        run.upds(2);
        s.button(1);
        run.upd();
        s.button(0);
        sleep_ms(60);
        run.upd();
        run.settle();
        /* a second click */
        s.button(1);
        run.upd();
        s.button(0);
        sleep_ms(60);
        run.upd();
        run.settle();
        run.fin();
    }
    check_log(ctx, "log");
}

/* what the idle messages during the wait can do */
CONF_TEST(gui_click_wait_idle)
{
    Session s;
    static const int rets[] = { D_CLOSE, D_REDRAW, D_REDRAWME, D_WANTFOCUS, 0x100, D_REDRAW_ALL };
    for(int i = 0; i < (int)(sizeof(rets) / sizeof(rets[0])); ++i)
    {
        Row r(4);
        log_add(conf::nm("idle in wait returns 0x%x", rets[i]));
        s.mouse(55, 15);
        Run run(r.dlg, -1);
        run.upds(2);
        s.button(1);
        run.upd();
        r.o[1].ret_once[MSG_IDLE] = rets[i];
        r.dlg[3].flags |= D_DIRTY;
        s.key(KEY_X, 'x');               /* not read while waiting */
        run.upd();
        run.upd();
        s.button(0);
        run.settle();
        run.upd();
        run.upd();
        input().keys.clear();
        run.fin();
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_dclick_range)
{
    Session s;
    static const int moves[][2] = { { 0, 0 }, { 7, 0 }, { 8, 0 }, { 9, 0 }, { -8, 0 }, { -9, 0 }, { 0, 8 },
                                    { 0, 9 }, { 0, -8 }, { 0, -9 }, { 8, 8 }, { 9, 9 } };
    for(int k = 0; k < (int)(sizeof(moves) / sizeof(moves[0])); ++k)
    {
        Obj a(0), b(1);
        DIALOG dlg[3];
        dlg[0] = obj(&a, 0, 0, 200, 200);
        dlg[1] = obj(&b, 50, 50, 60, 60);
        dlg[2] = end_obj();
        log_add(conf::nm("move %d,%d", moves[k][0], moves[k][1]));
        s.mouse(80, 80);
        Run run(dlg, -1);
        run.upd();
        s.button(1);
        run.upd();
        sleep_ms(30);
        s.button(0);
        sleep_ms(30);
        run.upd();
        s.mouse(80 + moves[k][0], 80 + moves[k][1]);
        sleep_ms(40);
        run.upd();
        s.button(1);
        sleep_ms(40);
        run.upd();
        s.button(0);
        sleep_ms(40);
        run.upd();
        run.settle();
        run.fin();
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_click_held_long)
{
    Session s;
    Row r(4);
    s.mouse(55, 15);
    Run run(r.dlg, -1);
    run.upds(2);
    s.button(1);
    run.upd();
    sleep_ms(500);                       /* the wait runs out while held */
    run.upd();
    run.upd();                           /* clicked again */
    sleep_ms(30);
    s.button(0);
    sleep_ms(30);
    run.upd();
    run.settle();
    run.upd();
    run.fin();
    check_log(ctx, "log");
}

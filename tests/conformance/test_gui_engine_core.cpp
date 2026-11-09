/*
 * test_gui_engine_core.cpp - the dialog manager without text: message order,
 * return flags, focus and mouse handling, with scripted input and logging
 * dialog procedures.
 */
#include "gui_conf.h"

using namespace guiconf;

/* 0 = background (wants nothing), 1..n = objects in a row */
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

CONF_TEST(gui_lifecycle)
{
    Session s;
    Row r(3);
    s.mouse(400, 400);
    Run run(r.dlg, -1);
    run.upds(3);
    s.key(KEY_TAB, 9);
    run.upd();
    s.key(KEY_TAB, 9);
    run.upd();
    s.mouse(15, 15);
    run.upd();
    s.mouse(15, 15, 1);
    run.upd();
    s.mouse(15, 15, 0);
    run.upd();
    run.settle();
    s.key(KEY_ESC, 27);
    run.upd();
    run.upd();
    run.fin();
    check_log(ctx, "log");
}

CONF_TEST(gui_init_focus)
{
    Session s;
    s.mouse(400, 400);
    for(int variant = 0; variant < 8; ++variant)
    {
        Row r(4);
        log_add(conf::nm("variant %d", variant));
        int focus = 1;
        switch(variant)
        {
        case 0: focus = 1; break;
        case 1: focus = 1; r.o[1].ret[MSG_WANTFOCUS] = 0; break;
        case 2: focus = 2; r.dlg[2].flags |= D_HIDDEN; break;
        case 3: focus = 2; r.dlg[2].flags |= D_DISABLED; break;
        case 4: focus = 1; r.dlg[3].flags |= D_GOTFOCUS; r.dlg[2].flags |= D_GOTMOUSE; break;
        case 5: focus = -1; r.dlg[3].flags |= D_GOTFOCUS | D_GOTMOUSE | D_DIRTY | D_SELECTED; break;
        case 6: focus = 3; r.dlg[3].flags |= D_GOTFOCUS; r.o[3].ret[MSG_WANTFOCUS] = 0; break;
        case 7: focus = 0; break;
        }
        Run run(r.dlg, focus);
        run.upds(2);
        run.fin();
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_init_mouse_over)
{
    Session s;
    Row r(4);
    s.mouse(55, 15);
    Run run(r.dlg, 1);
    run.upds(2);
    s.mouse(95, 15);
    run.upds(2);
    run.fin();
    check_log(ctx, "log");
}

CONF_TEST(gui_idle_flags)
{
    Session s;
    s.mouse(400, 400);
    static const int flags[] =
    {
        D_REDRAW, D_REDRAWME, D_WANTFOCUS, D_REDRAW_ALL, D_USED_CHAR, D_DONTWANTMOUSE,
        D_REDRAWME | D_WANTFOCUS, D_REDRAW | D_REDRAWME, 0x80, 0x100, D_CLOSE, D_CLOSE | D_REDRAW
    };
    for(int i = 0; i < (int)(sizeof(flags) / sizeof(flags[0])); ++i)
    {
        Row r(4);
        log_add(conf::nm("flags 0x%x", flags[i]));
        Run run(r.dlg, 1);
        run.upd();
        r.o[2].ret_once[MSG_IDLE] = flags[i];
        run.upds(3);
        run.fin();
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_start_end_flags)
{
    Session s;
    s.mouse(400, 400);
    static const int flags[] = { D_CLOSE, D_REDRAW, D_WANTFOCUS, D_REDRAWME };
    for(int i = 0; i < 4; ++i)
    {
        Row r(4);
        log_add(conf::nm("start flags 0x%x", flags[i]));
        r.o[2].ret[MSG_START] = flags[i];
        r.o[3].ret[MSG_END] = flags[i];
        r.o[1].ret[MSG_DRAW] = flags[i];
        Run run(r.dlg, -1);
        run.upds(2);
        run.fin();
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_tab_order)
{
    Session s;
    s.mouse(400, 400);
    Row r(6);
    r.o[3].ret[MSG_WANTFOCUS] = 0;
    r.dlg[4].flags |= D_HIDDEN;
    Run run(r.dlg, -1);
    run.upd();
    for(int i = 0; i < 6; ++i)
    {
        s.key(KEY_TAB, 9);
        run.upd();
    }
    s.shifts(KB_SHIFT_FLAG);
    for(int i = 0; i < 6; ++i)
    {
        s.key(KEY_TAB, 9);
        run.upd();
    }
    s.shifts(0);
    r.dlg[4].flags &= ~D_HIDDEN;
    r.dlg[5].flags |= D_DISABLED;
    for(int i = 0; i < 4; ++i)
    {
        s.key(KEY_TAB, 9);
        run.upd();
    }
    run.fin();
    check_log(ctx, "log");
}

CONF_TEST(gui_tab_lostfocus_keep)
{
    Session s;
    s.mouse(400, 400);
    Row r(4);
    r.o[1].ret[MSG_LOSTFOCUS] = D_WANTFOCUS;
    Run run(r.dlg, 1);
    run.upd();
    s.key(KEY_TAB, 9);
    run.upd();
    s.mouse(5, 200);
    run.upd();
    s.mouse(55, 15);
    run.upd();
    s.mouse(5, 200);
    run.upd();
    run.fin();
    check_log(ctx, "log");
}

CONF_TEST(gui_enter_space)
{
    Session s;
    s.mouse(400, 400);
    static const int keys[][2] =
    {
        { KEY_ENTER, 13 }, { KEY_SPACE, ' ' }, { KEY_ENTER_PAD, 13 }, { KEY_ENTER, 0 },
        { KEY_SPACE, 0 }, { KEY_A, 13 }, { KEY_A, ' ' }, { KEY_ESC, 27 }, { KEY_ESC, 0 }, { 0, 27 },
        { KEY_A, 'a' }, { KEY_A, 1 }, { KEY_TAB, 0 }, { 0, 9 }
    };
    for(int k = 0; k < (int)(sizeof(keys) / sizeof(keys[0])); ++k)
    {
        for(int focus = -1; focus <= 1; focus += 2)
        {
            Row r(3);
            log_add(conf::nm("key %d,%d focus %d", keys[k][0], keys[k][1], focus));
            Run run(r.dlg, focus);
            run.upd();
            s.key(keys[k][0], keys[k][1]);
            run.upds(2);
            run.fin();
        }
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_key_returns)
{
    Session s;
    s.mouse(400, 400);
    static const int rets[] = { D_CLOSE, D_REDRAW, D_REDRAWME, D_WANTFOCUS, D_USED_CHAR, D_CLOSE | D_USED_CHAR, D_REDRAW_ALL };
    for(int i = 0; i < (int)(sizeof(rets) / sizeof(rets[0])); ++i)
    {
        for(int where = 0; where < 4; ++where)
        {
            Row r(4);
            log_add(conf::nm("ret 0x%x where %d", rets[i], where));
            int m = where == 0 ? MSG_CHAR : where == 1 ? MSG_UCHAR : where == 2 ? MSG_XCHAR : MSG_KEY;
            r.o[1].ret_once[m] = rets[i];
            r.o[2].ret_once[m] = rets[i];
            Run run(r.dlg, 1);
            run.upd();
            s.key(KEY_X, 'x');
            run.upd();
            s.key(KEY_ENTER, 13);
            run.upd();
            s.key(KEY_ESC, 27);
            run.upds(2);
            run.fin();
        }
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_hotkeys)
{
    Session s;
    s.mouse(400, 400);
    static const int keys[][2] =
    {
        { KEY_A, 'a' }, { KEY_A, 'A' }, { KEY_A, 0 }, { KEY_A, 1 }, { KEY_B, 'b' }, { KEY_B, 2 },
        { KEY_B, 0 }, { KEY_C, 'c' }, { KEY_C, 0 }, { KEY_D, 'd' }, { KEY_D, 0 }, { KEY_1, '1' },
        { KEY_1, 0 }, { KEY_F1, 0 }, { 0, 'a' }, { KEY_E, 'e' }, { KEY_E, 0 }
    };
    for(int k = 0; k < (int)(sizeof(keys) / sizeof(keys[0])); ++k)
    {
        Row r(7);
        r.dlg[1].key = 'a';
        r.dlg[2].key = 'B';
        r.dlg[3].key = 3;      /* ctrl+c */
        r.dlg[4].key = 'd';
        r.dlg[4].flags |= D_DISABLED;
        r.dlg[5].key = '1';
        r.dlg[6].key = 'e';
        r.dlg[6].flags |= D_HIDDEN;
        log_add(conf::nm("key %d,%d", keys[k][0], keys[k][1]));
        Run run(r.dlg, -1);
        run.upd();
        s.key(keys[k][0], keys[k][1]);
        run.upds(2);
        run.fin();
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_hotkey_alt)
{
    Session s;
    s.mouse(400, 400);
    for(int sh = 0; sh < 4; ++sh)
    {
        int shifts = sh == 0 ? 0 : sh == 1 ? KB_ALT_FLAG : sh == 2 ? KB_CTRL_FLAG : KB_SHIFT_FLAG;
        Row r(4);
        r.dlg[1].key = 'a';
        r.dlg[2].key = 'b';
        log_add(conf::nm("shifts 0x%x", shifts));
        Run run(r.dlg, -1);
        run.upd();
        s.shifts(shifts);
        s.key(KEY_A, 0);
        run.upd();
        s.key(KEY_B, 'b');
        run.upd();
        s.key(KEY_B, 'B');
        run.upd();
        s.shifts(0);
        run.fin();
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_mouse_move)
{
    Session s;
    Row r(4);
    s.mouse(400, 400);
    Run run(r.dlg, -1);
    run.upd();
    s.mouse(15, 15);   /* 1 */
    run.upd();
    run.upd();
    s.mouse(55, 15);   /* 2 */
    run.upd();
    s.mouse(45, 15);   /* gap: background 0 */
    run.upd();
    s.mouse(39, 29);   /* edge of 1: x+w-1, y+h-1 */
    run.upd();
    s.mouse(40, 30);
    run.upd();
    s.mouse(10, 10);
    run.upd();
    s.mouse(9, 9);
    run.upd();
    s.mouse(-5, -5);
    run.upd();
    s.mouse(95, 15);   /* 3 */
    run.upd();
    gui_mouse_focus = FALSE;
    s.mouse(15, 15);
    run.upd();
    s.mouse(55, 15);
    run.upd();
    s.mouse(5, 200);
    run.upd();
    gui_mouse_focus = TRUE;
    s.mouse(15, 15);
    run.upd();
    run.fin();
    check_log(ctx, "log");
}

CONF_TEST(gui_mouse_overlap)
{
    Session s;
    Obj o[5];
    DIALOG dlg[6];
    for(int i = 0; i < 5; ++i)
    {
        o[i].id = i;
        o[i].ret[MSG_WANTFOCUS] = D_WANTFOCUS;
        dlg[i] = obj(&o[i], i * 5, i * 5, 100, 100);
    }
    dlg[5] = end_obj();
    o[4].ret[MSG_WANTMOUSE] = D_DONTWANTMOUSE;
    dlg[3].flags |= D_HIDDEN;
    o[2].ret[MSG_WANTFOCUS] = 0;
    s.mouse(400, 400);
    Run run(dlg, -1);
    run.upd();
    s.mouse(50, 50);
    run.upd();
    s.mouse(7, 7);
    run.upd();
    s.mouse(2, 2);
    run.upd();
    s.mouse(50, 50);
    dlg[2].flags |= D_DISABLED;
    run.upd();
    run.upd();
    dlg[3].flags &= ~D_HIDDEN;
    run.upd();
    run.fin();
    check_log(ctx, "log");
}

CONF_TEST(gui_mouse_buttons)
{
    Session s;
    static const int buttons[] = { 1, 2, 4, 3, 5, 6, 7 };
    for(int i = 0; i < (int)(sizeof(buttons) / sizeof(buttons[0])); ++i)
    {
        Row r(4);
        log_add(conf::nm("buttons %d", buttons[i]));
        s.mouse(55, 15);
        Run run(r.dlg, -1);
        run.upds(2);
        s.button(buttons[i]);
        run.upd();
        run.upd();
        s.button(0);
        run.upd();
        run.settle();
        run.upd();
        run.fin();
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_click_returns)
{
    Session s;
    static const int rets[] = { D_CLOSE, D_REDRAW, D_REDRAWME, D_WANTFOCUS, D_USED_CHAR, D_REDRAW_ALL };
    for(int i = 0; i < (int)(sizeof(rets) / sizeof(rets[0])); ++i)
    {
        for(int m = 0; m < 3; ++m)
        {
            int msg = m == 0 ? MSG_CLICK : m == 1 ? MSG_LPRESS : MSG_LRELEASE;
            Row r(4);
            r.o[2].ret_once[msg] = rets[i];
            r.o[2].ret[MSG_WANTFOCUS] = 0;
            log_add(conf::nm("ret 0x%x msg %s", rets[i], msg_name(msg)));
            s.mouse(55, 15);
            Run run(r.dlg, 1);
            run.upds(2);
            s.button(1);
            run.upd();
            s.button(0);
            run.upd();
            run.settle();
            run.upd();
            run.fin();
        }
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_click_disabled)
{
    Session s;
    Row r(4);
    r.dlg[2].flags |= D_DISABLED;
    s.mouse(55, 15);
    Run run(r.dlg, 1);
    run.upds(2);
    s.button(1);
    run.upd();
    s.button(0);
    run.upd();
    run.settle();
    s.mouse(95, 15);
    run.upd();
    s.button(1);
    run.upd();
    s.button(0);
    run.upd();
    run.settle();
    run.fin();
    check_log(ctx, "log");
}

CONF_TEST(gui_click_held)
{
    Session s;
    Row r(4);
    s.mouse(55, 15);
    Run run(r.dlg, -1);
    run.upds(2);
    s.button(1);
    run.upd();
    run.upd();
    s.mouse(95, 15, 1);   /* drag onto 3 while held */
    run.upd();
    run.settle();
    run.upd();
    s.mouse(95, 15, 0);
    run.upd();
    run.settle();
    run.fin();
    check_log(ctx, "log");
}

/* The reference notices button changes for the double click in a timer, so
 * every change is followed by a pause before the next update. */
CONF_TEST(gui_dclick)
{
    Session s;
    for(int variant = 0; variant < 7; ++variant)
    {
        Row r(4);
        log_add(conf::nm("variant %d", variant));
        s.mouse(55, 15);
        if(variant == 5)
            r.o[2].ret_once[MSG_DCLICK] = D_CLOSE;
        if(variant == 6)
            r.o[2].ret_once[MSG_DCLICK] = D_REDRAW | D_WANTFOCUS;
        Run run(r.dlg, -1);
        run.upds(2);
        s.button(1);
        run.upd();
        sleep_ms(40);
        s.button(0);
        sleep_ms(60);
        run.upd();
        if(variant == 1)
            s.mouse(95, 15);          /* moved onto another object */
        if(variant == 3)
            s.mouse(57, 16);          /* moved a little */
        if(variant == 4)
            s.mouse(55, 9);           /* off the object, but close */
        sleep_ms(60);
        s.button(variant == 2 ? 2 : 1);
        sleep_ms(60);
        run.upd();
        s.button(0);
        sleep_ms(60);
        run.upd();
        run.settle();
        run.upd();
        run.fin();
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_dclick_late)
{
    Session s;
    Row r(4);
    s.mouse(55, 15);
    Run run(r.dlg, -1);
    run.upds(2);
    s.button(1);
    run.upd();
    sleep_ms(40);
    s.button(0);
    sleep_ms(500);                    /* too slow for a double click */
    run.upd();
    s.button(1);
    sleep_ms(60);
    run.upd();
    s.button(0);
    sleep_ms(60);
    run.upd();
    run.settle();
    run.upd();
    run.fin();
    check_log(ctx, "log");
}

CONF_TEST(gui_wheel)
{
    Session s;
    s.mouse(400, 400);
    Row r(4);
    Run run(r.dlg, -1);
    run.upd();
    s.wheel(1);
    run.upd();
    run.upd();
    s.key(KEY_TAB, 9);
    run.upd();
    s.wheel(4);
    run.upd();
    s.wheel(-2);
    run.upd();
    s.mouse(95, 15);
    s.wheel(-1);
    run.upd();
    r.o[3].ret_once[MSG_WHEEL] = D_CLOSE;
    s.wheel(0);
    run.upd();
    run.fin();
    s.wheel(0);
    check_log(ctx, "log");
}

CONF_TEST(gui_dirty)
{
    Session s;
    s.mouse(400, 400);
    Row r(4);
    Run run(r.dlg, -1);
    run.upd();
    r.dlg[2].flags |= D_DIRTY;
    run.upd();
    r.dlg[1].flags |= D_DIRTY;
    r.dlg[3].flags |= D_DIRTY | D_HIDDEN;
    run.upd();
    run.upd();
    r.dlg[3].flags &= ~D_HIDDEN;
    run.upd();
    r.o[1].ret_once[MSG_IDLE] = D_REDRAWME;
    r.dlg[2].flags |= D_DIRTY;
    run.upd();
    run.upd();
    run.fin();
    check_log(ctx, "log");
}

CONF_TEST(gui_hidden_draw)
{
    Session s;
    s.mouse(400, 400);
    Row r(4);
    r.dlg[2].flags |= D_HIDDEN;
    r.o[1].ret_once[MSG_IDLE] = D_REDRAW;
    Run run(r.dlg, -1);
    run.upds(3);
    run.fin();
    check_log(ctx, "log");
}

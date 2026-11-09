/*
 * test_gui_engine_misc.cpp - the message helpers (object_message,
 * dialog_message, broadcast_dialog_message), nested dialogs and
 * active_dialog, the dialog utilities, do_dialog() and the player fields.
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

    explicit Row(int count, int want_focus_mask = 0xFE, int id_base = 0)
    {
        n = count;
        for(int i = 0; i < n; ++i)
        {
            o[i].id = id_base + i;
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

CONF_TEST(gui_dialog_message_all)
{
    Session s;
    s.mouse(-100, -100);
    for(int msg = 1; msg <= MSG_USER + 1; ++msg)
    {
        Row r(5);
        r.dlg[2].flags |= D_HIDDEN;
        r.dlg[3].flags |= D_DISABLED;
        for(int i = 0; i < 5; ++i)
            r.o[i].ret[msg] = (i == 1) ? 0x100 : (i == 2) ? (0x200 | D_REDRAWME) : (i == 3) ? (0x400 | D_REDRAWME) : 0;
        r.dlg[4].flags |= D_DIRTY;
        int obj_ = 77;
        log_add(conf::nm("message %s", msg_name(msg)));
        int ret = dialog_message(r.dlg, msg, 0x1234, &obj_);
        log_add(conf::nm("ret 0x%x obj %d", ret, obj_));
        log_step(0, 0, 0, r.dlg);
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_object_message)
{
    Session s;
    s.mouse(-100, -100);
    Row r(3);
    for(int msg = 1; msg <= MSG_USER + 1; ++msg)
    {
        for(int v = 0; v < 3; ++v)
        {
            r.dlg[1].flags = (v == 1) ? D_HIDDEN : (v == 2) ? D_DISABLED : 0;
            r.o[1].ret[msg] = D_REDRAWME | D_REDRAW | 0x1000;
            int ret = object_message(&r.dlg[1], msg, 7);
            log_add(conf::nm("%s v%d ret 0x%x flags %s", msg_name(msg), v, ret, flag_text(r.dlg[1].flags).c_str()));
            r.dlg[1].flags = 0;
        }
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_broadcast_nested)
{
    Session s;
    s.mouse(-100, -100);
    Row a(3, 0xFE, 0), b(3, 0xFE, 10);
    a.o[1].ret[MSG_USER] = 0x100;
    b.o[1].ret[MSG_USER] = 0x200;
    log_add(conf::nm("no dialog: 0x%x", broadcast_dialog_message(MSG_USER, 1)));
    log_add(conf::nm("active null %d", active_dialog == NULL));
    DIALOG_PLAYER *pa = init_dialog(a.dlg, -1);
    log_add(conf::nm("active a %d", active_dialog == a.dlg));
    update_dialog(pa);
    DIALOG_PLAYER *pb = init_dialog(b.dlg, -1);
    log_add(conf::nm("active b %d", active_dialog == b.dlg));
    log_add(conf::nm("broadcast 0x%x", broadcast_dialog_message(MSG_USER, 3)));
    update_dialog(pb);
    update_dialog(pb);
    /* the inner dialog asks for everything to be redrawn */
    b.o[1].ret_once[MSG_IDLE] = D_REDRAW_ALL;
    log_add("inner idles with D_REDRAW_ALL");
    update_dialog(pb);
    log_add("inner update");
    update_dialog(pb);
    log_add("inner update");
    update_dialog(pb);
    log_add(conf::nm("inner shutdown %d", shutdown_dialog(pb)));
    log_add(conf::nm("active a %d", active_dialog == a.dlg));
    log_add("outer update");
    update_dialog(pa);
    log_add("outer update");
    update_dialog(pa);
    log_add(conf::nm("outer shutdown %d", shutdown_dialog(pa)));
    log_add(conf::nm("active null %d", active_dialog == NULL));
    /* shut down out of order */
    pa = init_dialog(a.dlg, -1);
    pb = init_dialog(b.dlg, -1);
    shutdown_dialog(pa);
    log_add(conf::nm("ooo: b %d a %d null %d", active_dialog == b.dlg, active_dialog == a.dlg, active_dialog == NULL));
    log_add(conf::nm("broadcast 0x%x", broadcast_dialog_message(MSG_USER, 4)));
    shutdown_dialog(pb);
    log_add(conf::nm("ooo: b %d a %d null %d", active_dialog == b.dlg, active_dialog == a.dlg, active_dialog == NULL));
    /* a redraw-all from the outer dialog while an inner one exists */
    pa = init_dialog(a.dlg, -1);
    pb = init_dialog(b.dlg, -1);
    update_dialog(pb);
    a.o[2].ret_once[MSG_IDLE] = D_REDRAW_ALL;
    log_add("outer idles with D_REDRAW_ALL");
    update_dialog(pa);
    log_add("outer update");
    update_dialog(pa);
    log_add("inner update");
    update_dialog(pb);
    shutdown_dialog(pb);
    shutdown_dialog(pa);
    check_log(ctx, "log");
}

CONF_TEST(gui_dialog_utilities)
{
    Session s;
    Row r(5);
    r.dlg[0].x = 30; r.dlg[0].y = 40; r.dlg[0].w = 100; r.dlg[0].h = 50;
    r.dlg[2].x = 5; r.dlg[2].y = 60;
    r.dlg[3].x = -20; r.dlg[3].y = 300; r.dlg[3].w = 7; r.dlg[3].h = 1;
    r.dlg[4].w = 0; r.dlg[4].h = 0;
    conf::Seq q(ctx, "positions");
    static const int pos[][2] = { { 100, 120 }, { 0, 0 }, { -50, -60 }, { 1, 2 } };
    for(int k = 0; k < 4; ++k)
    {
        position_dialog(r.dlg, pos[k][0], pos[k][1]);
        for(int i = 0; i < 5; ++i)
            q.add_str(conf::nm("%d,%d", r.dlg[i].x, r.dlg[i].y), "position %d,%d obj %d", pos[k][0], pos[k][1], i);
        centre_dialog(r.dlg);
        for(int i = 0; i < 5; ++i)
            q.add_str(conf::nm("%d,%d", r.dlg[i].x, r.dlg[i].y), "centre after %d obj %d", k, i);
        r.dlg[0].w += 1;
        r.dlg[0].h += 3;
    }
    q.end();
    set_dialog_color(r.dlg, 7, 9);
    for(int i = 0; i < 5; ++i)
        ctx.checkf(conf::nm("colors%d", i), "%d %d", r.dlg[i].fg, r.dlg[i].bg);
    ctx.check_int("find_none", find_dialog_focus(r.dlg));
    r.dlg[3].flags |= D_GOTFOCUS;
    r.dlg[4].flags |= D_GOTFOCUS;
    ctx.check_int("find_3", find_dialog_focus(r.dlg));
    r.dlg[0].flags |= D_GOTFOCUS | D_HIDDEN;
    ctx.check_int("find_0", find_dialog_focus(r.dlg));
    ctx.check_bool("gui_screen", gui_get_screen() == s.scr);
    gui_set_screen(NULL);
    ctx.check_bool("gui_screen_null_is_screen", gui_get_screen() == screen);
    gui_set_screen(s.scr);
}

/* init_dialog() clips the GUI bitmap to the graphics mode's screen size
 * (none here: an empty clip rectangle), whatever bitmap it is. */
CONF_TEST(gui_init_clip)
{
    Session s(64, 40);
    BITMAP *other = create_bitmap_ex(8, 30, 20);
    for(int v = 0; v < 4; ++v)
    {
        BITMAP *g = (v & 1) ? other : s.scr;
        screen = (v & 2) ? other : s.scr;
        gui_set_screen(g);
        set_clip_rect(g, 2, 3, 10, 11);
        Obj o0(0);
        DIALOG dlg[2];
        dlg[0] = obj(&o0, 0, 0, 64, 40);
        dlg[1] = end_obj();
        DIALOG_PLAYER *p = init_dialog(dlg, -1);
        ctx.check_clip(conf::nm("init%d", v), g);
        set_clip_rect(g, 2, 3, 10, 11);
        update_dialog(p);
        ctx.check_clip(conf::nm("update%d", v), g);
        shutdown_dialog(p);
        ctx.check_clip(conf::nm("shutdown%d", v), g);
        log().clear();
    }
    screen = s.scr;
    gui_set_screen(s.scr);
    destroy_bitmap(other);
}

/* Draw results of objects drawn because they were marked dirty. */
CONF_TEST(gui_dirty_draw_results)
{
    Session s;
    s.mouse(-100, -100);
    static const int rets[] = { 0x100, D_CLOSE, D_REDRAW, D_WANTFOCUS, D_REDRAWME, D_USED_CHAR };
    for(int i = 0; i < (int)(sizeof(rets) / sizeof(rets[0])); ++i)
    {
        Row r(4);
        log_add(conf::nm("dirty draw returns 0x%x", rets[i]));
        Run run(r.dlg, 1);
        run.upd();
        r.dlg[2].flags |= D_DIRTY;
        r.o[2].ret_once[MSG_DRAW] = rets[i];
        run.upd();
        run.upd();
        /* dirty while idling */
        r.o[3].ret_once[MSG_IDLE] = D_REDRAWME;
        r.o[3].ret_once[MSG_DRAW] = rets[i];
        run.upd();
        run.upd();
        run.upd();
        run.fin();
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_shutdown_flags)
{
    Session s;
    Row r(5);
    r.dlg[4].flags |= D_GOTMOUSE;
    s.mouse(55, 15);
    Run run(r.dlg, 1);
    run.upd();
    r.dlg[3].flags |= D_DIRTY | D_SELECTED;
    run.fin();
    log_step(0, 0, 0, r.dlg);
    /* start again with the flags left behind */
    s.mouse(95, 15);
    Run again(r.dlg, -1);
    again.upd();
    again.fin();
    log_step(0, 0, 0, r.dlg);
    check_log(ctx, "log");
}

CONF_TEST(gui_player_fields)
{
    Session s;
    Row r(4);
    s.mouse(55, 16, 0);
    s.wheel(3);
    DIALOG_PLAYER *p = init_dialog(r.dlg, -1);
    ctx.checkf("init", "obj %d focus %d mouse %d joy_on %d click_wait %d oz %d b %d", p->obj, p->focus_obj,
               p->mouse_obj, p->joy_on, p->click_wait, p->mouse_oz, p->mouse_b);
    ctx.check_bool("dialog", p->dialog == r.dlg);
    update_dialog(p);
    ctx.checkf("update", "obj %d focus %d mouse %d joy_on %d click_wait %d oz %d b %d", p->obj, p->focus_obj,
               p->mouse_obj, p->joy_on, p->click_wait, p->mouse_oz, p->mouse_b);
    s.mouse(56, 17, 1);
    s.wheel(5);
    update_dialog(p);
    ctx.checkf("click", "obj %d focus %d mouse %d joy_on %d click_wait %d ox %d oy %d oz %d b %d", p->obj,
               p->focus_obj, p->mouse_obj, p->joy_on, p->click_wait, p->mouse_ox, p->mouse_oy, p->mouse_oz, p->mouse_b);
    s.mouse(56, 17, 0);
    sleep_ms(DCLICK_SETTLE_MS);
    update_dialog(p);
    update_dialog(p);
    ctx.checkf("after", "obj %d focus %d mouse %d joy_on %d click_wait %d ox %d oy %d oz %d b %d", p->obj,
               p->focus_obj, p->mouse_obj, p->joy_on, p->click_wait, p->mouse_ox, p->mouse_oy, p->mouse_oz, p->mouse_b);
    /* the game sets joy_on itself (zc_sys.cpp) */
    p->joy_on = TRUE;
    update_dialog(p);
    ctx.checkf("joy_on", "%d", p->joy_on);
    shutdown_dialog(p);
    log().clear();
    s.wheel(0);
}

/* do_dialog() and the dialog closing in various ways */
namespace
{
    int g_idle_count;
    int g_close_after;
    int g_close_value;

    int closing_extra(int msg, DIALOG *d, int c)
    {
        (void)d;
        (void)c;
        if(msg == MSG_IDLE && ++g_idle_count == g_close_after)
            return g_close_value;
        return -1;
    }
}

CONF_TEST(gui_do_dialog)
{
    Session s;
    s.mouse(-100, -100);
    static const int closes[] = { D_CLOSE, D_CLOSE | D_REDRAW, D_EXIT, 0 };
    for(int v = 0; v < 4; ++v)
    {
        Row r(4);
        g_idle_count = 0;
        g_close_after = 4;
        g_close_value = closes[v];
        r.o[2].extra = closing_extra;
        if(closes[v] != D_CLOSE && closes[v] != (D_CLOSE | D_REDRAW))
        {
            /* not a close: ESC ends it */
            s.key(KEY_X, 'x');
            s.key(KEY_ESC, 27);
        }
        log_add(conf::nm("do_dialog variant %d", v));
        int ret = do_dialog(r.dlg, 1);
        log_add(conf::nm("returned %d", ret));
        log_step(0, 0, 0, r.dlg);
        input().keys.clear();
    }
    /* a click on an object that closes */
    {
        Row r(4);
        r.o[2].ret[MSG_CLICK] = D_CLOSE;
        s.mouse(55, 15, 1);
        log_add("do_dialog click");
        int ret = do_dialog(r.dlg, -1);
        log_add(conf::nm("returned %d", ret));
        s.mouse(-100, -100, 0);
    }
    check_log(ctx, "log");
}

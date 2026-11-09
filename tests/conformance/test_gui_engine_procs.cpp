/*
 * test_gui_engine_procs.cpp - the standard dialog procedures that need no
 * text, colour or blitting functions: d_clear_proc, d_box_proc,
 * d_yield_proc, d_keyboard_proc. Every message is sent to them directly;
 * return values, flags, what they draw (on the GUI bitmap, not on 'screen')
 * and the clip rectangles are compared. (d_shadow_box_proc needs makecol():
 * test_gui_text_procs.cpp.)
 */
#include "gui_conf.h"

using namespace guiconf;

namespace
{

struct Geometry { int x, y, w, h; };

const Geometry k_geom[] =
{
    { 10, 12, 30, 20 }, { 0, 0, 64, 48 }, { -5, -7, 20, 15 }, { 50, 40, 30, 20 }, { 20, 20, 1, 1 },
    { 20, 20, 2, 2 }, { 20, 20, 3, 3 }, { 20, 20, 0, 0 }, { 20, 20, -4, 6 }, { 60, 45, 10, 10 }
};

void run_draw_cases(conf::Ctx &ctx, const char *name, DIALOG_PROC proc)
{
    Session s(64, 48);
    BITMAP *other = create_bitmap_ex(8, 64, 48);
    screen = other;                       /* drawing must go to the GUI bitmap */
    conf::Seq q(ctx, conf::nm("%s_draw", name));
    for(size_t g = 0; g < sizeof(k_geom) / sizeof(k_geom[0]); ++g)
    {
        for(int v = 0; v < 6; ++v)
        {
            clear_to_color(s.scr, 3);
            clear_to_color(other, 4);
            int flags = (v == 1) ? D_DISABLED : (v == 2) ? D_SELECTED : (v == 3) ? D_GOTFOCUS : 0;
            DIALOG d = proc_obj(proc, k_geom[g].x, k_geom[g].y, k_geom[g].w, k_geom[g].h,
                                (v == 4) ? 0 : 200, (v == 5) ? 255 : 17, 0, flags, 0, 0);
            gui_mg_color = 99;
            if(v == 5)
                set_clip_rect(s.scr, 5, 6, 40, 30);
            int r = proc(MSG_DRAW, &d, 0);
            q.add_str(conf::nm("%d %d %d %d %d", s.scr->clip, s.scr->cl, s.scr->ct, s.scr->cr, s.scr->cb),
                      "gui clip geom %d variant %d", (int)g, v);
            q.add_str(conf::nm("%d %d %d %d %d", other->clip, other->cl, other->ct, other->cr, other->cb),
                      "screen clip geom %d variant %d", (int)g, v);
            set_clip_rect(s.scr, 0, 0, 63, 47);
            set_clip_rect(other, 0, 0, 63, 47);
            q.add_int(r, "ret geom %d variant %d", (int)g, v);
            q.add_bitmap(s.scr, "gui geom %d variant %d", (int)g, v);
            q.add_bitmap(other, "screen geom %d variant %d", (int)g, v);
            q.add_int(d.flags, "flags geom %d variant %d", (int)g, v);
        }
    }
    q.end();
    screen = s.scr;
    destroy_bitmap(other);
}

void run_messages(conf::Ctx &ctx, const char *name, DIALOG_PROC proc)
{
    Session s(64, 48);
    conf::Seq q(ctx, conf::nm("%s_messages", name));
    for(int msg = 1; msg <= MSG_USER + 1; ++msg)
    {
        if(msg == MSG_DRAW)
            continue;
        for(int v = 0; v < 2; ++v)
        {
            clear_to_color(s.scr, 3);
            DIALOG d = proc_obj(proc, 10, 10, 20, 20, 200, 17, 0, v ? D_DISABLED | D_SELECTED : 0, 0, 0);
            int r = proc(msg, &d, 0x1e61);
            q.add_int(r, "ret %s v%d", msg_name(msg), v);
            q.add_str(conf::nm("%d,%d,%d,%d f=%s", d.x, d.y, d.w, d.h, flag_text(d.flags).c_str()), "obj %s v%d", msg_name(msg), v);
            q.add_bitmap(s.scr, "gui %s v%d", msg_name(msg), v);
        }
    }
    q.end();
}

}

CONF_TEST(gui_d_clear_proc)
{
    run_draw_cases(ctx, "clear", d_clear_proc);
    run_messages(ctx, "clear", d_clear_proc);

    /* which size it clears: the GUI bitmap's, or the graphics mode's when
     * the GUI bitmap is 'screen' (no mode here: nothing) */
    BITMAP *old = screen;
    BITMAP *a = create_bitmap_ex(8, 20, 14);
    BITMAP *b = create_bitmap_ex(8, 30, 20);
    BITMAP *sub = create_sub_bitmap(b, 2, 3, 10, 9);
    BITMAP *cases[][2] = { { a, a }, { a, b }, { sub, b }, { b, sub }, { b, NULL } };
    for(int k = 0; k < 5; ++k)
    {
        screen = cases[k][1];
        gui_set_screen(cases[k][0]);
        clear_to_color(a, 3);
        clear_to_color(b, 4);
        set_clip_rect(cases[k][0], 1, 2, 10, 11);
        DIALOG c = proc_obj(d_clear_proc, 2, 2, 12, 8, 200, 17, 0, 0, 0, 0);
        ctx.check_int(conf::nm("which_ret%d", k), d_clear_proc(MSG_DRAW, &c, 0));
        ctx.check_clip(conf::nm("which_clip%d", k), cases[k][0]);
        ctx.check_bitmap(conf::nm("which_a%d", k), a);
        ctx.check_bitmap(conf::nm("which_b%d", k), b);
    }
    screen = old;
    gui_set_screen(NULL);
    destroy_bitmap(sub);
    destroy_bitmap(a);
    destroy_bitmap(b);
}

CONF_TEST(gui_d_box_proc)
{
    run_draw_cases(ctx, "box", d_box_proc);
    run_messages(ctx, "box", d_box_proc);
}

CONF_TEST(gui_d_yield_proc)
{
    run_messages(ctx, "yield", d_yield_proc);
    run_draw_cases(ctx, "yield", d_yield_proc);
}

namespace
{
    int g_kb_calls;
    int g_kb_ret;

    int keyboard_callback_fn(void)
    {
        ++g_kb_calls;
        log_add(conf::nm("callback %d", g_kb_calls));
        return g_kb_ret;
    }
}

CONF_TEST(gui_d_keyboard_proc_messages)
{
    Session s(64, 48);
    conf::Seq q(ctx, "messages");
    static const int keys[] = { 0x1e61, 0x1e00, 0x1e41, 0x3b00, 0x3c00, 0x0061, 0x2e03, 0x4300, 0x430d, 0 };
    for(int msg = 1; msg <= MSG_USER + 1; ++msg)
    {
        for(int k = 0; k < (int)(sizeof(keys) / sizeof(keys[0])); ++k)
        {
            for(int v = 0; v < 4; ++v)
            {
                g_kb_calls = 0;
                g_kb_ret = (v == 3) ? D_CLOSE : 0x100;
                DIALOG d = proc_obj(d_keyboard_proc, 10, 10, 20, 20, 200, 17,
                                    (v == 0) ? 'a' : (v == 1) ? 3 : 0, (v == 2) ? D_DISABLED : 0,
                                    (v == 2) ? KEY_F1 : 0, (v == 1) ? KEY_F2 : (v == 3) ? KEY_ENTER : 0,
                                    (void *)keyboard_callback_fn);
                clear_to_color(s.scr, 3);
                int r = d_keyboard_proc(msg, &d, keys[k]);
                q.add_str(conf::nm("ret 0x%x calls %d %d,%d,%d,%d f=%s", r, g_kb_calls, d.x, d.y, d.w, d.h,
                                   flag_text(d.flags).c_str()), "%s key 0x%x variant %d", msg_name(msg), keys[k], v);
                q.add_bitmap(s.scr, "gui %s key 0x%x variant %d", msg_name(msg), keys[k], v);
            }
        }
    }
    q.end();
    log().clear();
}

/* d_keyboard_proc inside a running dialog */
CONF_TEST(gui_d_keyboard_proc_dialog)
{
    Session s;
    s.mouse(-100, -100);
    Obj o0(0), o1(1), o2(2);
    o1.ret[MSG_WANTFOCUS] = D_WANTFOCUS;
    DIALOG dlg[6];
    dlg[0] = obj(&o0, 0, 0, 320, 240);
    dlg[1] = obj(&o1, 10, 10, 30, 20);
    dlg[2] = proc_obj(d_keyboard_proc, 0, 0, 0, 0, 0, 0, 'q', 0, KEY_F1, 0, (void *)keyboard_callback_fn);
    dlg[3] = proc_obj(d_keyboard_proc, 0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_ESC, (void *)keyboard_callback_fn);
    dlg[4] = obj(&o2, 50, 10, 30, 20);
    dlg[5] = end_obj();
    static const int rets[] = { D_O_K, D_REDRAW, D_CLOSE };
    for(int i = 0; i < 3; ++i)
    {
        g_kb_calls = 0;
        g_kb_ret = rets[i];
        log_add(conf::nm("callback returns 0x%x", rets[i]));
        Run run(dlg, 1);
        run.upd();
        s.key(KEY_Q, 'q');
        run.upd();
        s.key(KEY_Q, 'Q');
        run.upd();
        s.key(KEY_F1, 0);
        run.upd();
        s.key(KEY_ESC, 27);
        run.upd();
        s.key(KEY_A, 'a');
        run.upd();
        input().keys.clear();
        run.fin();
    }
    check_log(ctx, "log");
}

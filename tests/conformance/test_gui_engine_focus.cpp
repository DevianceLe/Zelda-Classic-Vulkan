/*
 * test_gui_engine_focus.cpp - focus handling of the dialog manager:
 * offer_focus(), Tab / Shift+Tab, the arrow keys (distance order, ties,
 * hidden and disabled objects), keyboard shortcuts.
 */
#include "gui_conf.h"

#include <stdlib.h>
#include <vector>

using namespace guiconf;

namespace
{

/* 0 = background (wants nothing), 1..n-1 = objects in a row */
struct Row
{
    Obj o[12];
    DIALOG dlg[13];
    int n;

    explicit Row(int count, int want_focus_mask = 0xFFE)
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

/* runs one key press on a dialog whose focus is 'focus' and logs it */
void press(Session &s, DIALOG *dlg, int focus, int scancode, int ascii, int shifts = 0)
{
    log_add(conf::nm("press %d,0x%x shifts %d focus %d", scancode, ascii, shifts, focus));
    Run run(dlg, focus);
    run.upd();
    s.shifts(shifts);
    s.key(scancode, ascii);
    run.upd();
    s.shifts(0);
    run.fin();
}

}

CONF_TEST(gui_offer_focus_matrix)
{
    Session s;
    s.mouse(-100, -100);
    for(int v = 0; v < 64; ++v)
    {
        Row r(4);
        int focus = (v & 1) ? 1 : -1;
        int target = (v & 2) ? 2 : -1;
        r.o[2].ret[MSG_WANTFOCUS] = (v & 4) ? 0x2000 : (D_WANTFOCUS | 0x4000);
        if(v & 8) r.o[1].ret[MSG_LOSTFOCUS] = D_WANTFOCUS | 0x1000;
        else r.o[1].ret[MSG_LOSTFOCUS] = 0x1000;
        if(v & 16) r.dlg[2].flags |= D_HIDDEN;
        if(v & 32) r.dlg[2].flags |= D_DISABLED;
        r.o[1].ret[MSG_GOTFOCUS] = 0x100;
        r.o[2].ret[MSG_GOTFOCUS] = 0x200 | D_REDRAWME;
        r.o[1].ret[MSG_DRAW] = 0x400;
        r.o[2].ret[MSG_DRAW] = 0x800;
        for(int force = 0; force < 2; ++force)
        {
            int f = focus;
            r.dlg[1].flags &= ~(D_GOTFOCUS | D_DIRTY);
            r.dlg[2].flags &= ~(D_GOTFOCUS | D_DIRTY);
            if(focus >= 0)
                r.dlg[focus].flags |= D_GOTFOCUS;
            log_add(conf::nm("v%d force %d", v, force));
            int ret = offer_focus(r.dlg, target, &f, force);
            log_add(conf::nm("ret 0x%x focus %d flags %s %s", ret, f,
                             flag_text(r.dlg[1].flags).c_str(), flag_text(r.dlg[2].flags).c_str()));
        }
    }
    /* offering the focus to the object that has it */
    {
        Row r(4);
        r.dlg[1].flags |= D_GOTFOCUS;
        for(int force = 0; force < 2; ++force)
        {
            int f = 1;
            int ret = offer_focus(r.dlg, 1, &f, force);
            log_add(conf::nm("same force %d: ret 0x%x focus %d flags %s", force, ret, f, flag_text(r.dlg[1].flags).c_str()));
        }
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_tab_keys)
{
    Session s;
    s.mouse(-100, -100);
    static const int keys[][3] =
    {
        { KEY_TAB, 9, 0 }, { KEY_TAB, 0, 0 }, { KEY_TAB, 'x', 0 }, { KEY_TAB, 9, KB_SHIFT_FLAG },
        { KEY_TAB, 0, KB_SHIFT_FLAG }, { 0, 9, 0 }, { KEY_TAB, 9, KB_CTRL_FLAG }, { KEY_TAB, 13, 0 },
        { KEY_TAB, ' ', 0 }, { KEY_TAB, 27, 0 }
    };
    for(int k = 0; k < (int)(sizeof(keys) / sizeof(keys[0])); ++k)
    {
        for(int f = -1; f < 7; f += 2)
        {
            Row r(7);
            r.o[3].ret[MSG_WANTFOCUS] = 0;
            r.dlg[5].flags |= D_HIDDEN;
            r.dlg[6].flags |= (k & 1) ? D_DISABLED : 0;
            press(s, r.dlg, f, keys[k][0], keys[k][1], keys[k][2]);
        }
    }
    /* nobody else wants the focus */
    {
        Row r(4, 0x2);
        press(s, r.dlg, 1, KEY_TAB, 9);
        press(s, r.dlg, 1, KEY_TAB, 0);
        press(s, r.dlg, 1, KEY_RIGHT, 0);
    }
    /* a single object */
    {
        Obj o(0);
        o.ret[MSG_WANTFOCUS] = D_WANTFOCUS;
        DIALOG d[2];
        d[0] = obj(&o, 0, 0, 10, 10);
        d[1] = end_obj();
        press(s, d, 0, KEY_TAB, 9);
        press(s, d, -1, KEY_TAB, 0);
        press(s, d, 0, KEY_DOWN, 0);
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_tab_returns)
{
    Session s;
    s.mouse(-100, -100);
    static const int rets[] = { D_CLOSE, D_REDRAW, D_REDRAWME, D_WANTFOCUS, D_USED_CHAR, 0x100 };
    for(int i = 0; i < (int)(sizeof(rets) / sizeof(rets[0])); ++i)
    {
        Row r(5);
        r.o[2].ret_once[MSG_GOTFOCUS] = rets[i];
        r.o[1].ret_once[MSG_LOSTFOCUS] = rets[i];
        r.o[3].ret_once[MSG_WANTFOCUS] = rets[i] | D_WANTFOCUS;
        log_add(conf::nm("ret 0x%x", rets[i]));
        Run run(r.dlg, 1);
        run.upd();
        s.key(KEY_TAB, 9);
        run.upd();
        s.key(KEY_TAB, 9);
        run.upd();
        run.upd();
        run.fin();
    }
    check_log(ctx, "log");
}

/* Arrow keys: random layouts; most objects refuse the focus, so the order in
 * which they are asked is visible. */
CONF_TEST(gui_arrow_keys)
{
    Session s(1200, 1200);
    s.mouse(-1000, -1000);
    conf::Rng &rng = ctx.rng;
    static const int keys[4] = { KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN };
    for(int t = 0; t < 400; ++t)
    {
        int n = rng.range(2, 12);
        Obj o[12];
        DIALOG dlg[13];
        int spread = (t % 4 == 0) ? 3 : 200;     /* small spread: many ties */
        for(int i = 0; i < n; ++i)
        {
            o[i].id = i;
            int w = rng.range(1, 60), h = rng.range(1, 60);
            if(t % 4 == 0)
                w = h = 10;
            int flags = 0;
            if(t % 3 == 2 && rng.range(0, 4) == 0)
                flags = rng.range(0, 1) ? D_HIDDEN : D_DISABLED;
            dlg[i] = obj(&o[i], 100 + rng.range(-20, spread) * (t % 4 == 0 ? 20 : 1),
                         100 + rng.range(-20, spread) * (t % 4 == 0 ? 20 : 1), w, h, flags);
            if(rng.range(0, 5) == 0)
                o[i].ret[MSG_WANTFOCUS] = D_WANTFOCUS;
        }
        dlg[n] = end_obj();
        int focus = rng.range(-1, n - 1);
        if(focus >= 0)
        {
            dlg[focus].flags &= ~D_HIDDEN;
            o[focus].ret[MSG_WANTFOCUS] = D_WANTFOCUS;
        }
        int k = rng.range(0, 3);
        log_add(conf::nm("trial %d key %d focus %d", t, keys[k], focus));
        DIALOG_PLAYER *p = init_dialog(dlg, focus);
        update_dialog(p);
        log().clear();
        log_add(conf::nm("trial %d", t));
        s.key(keys[k], 0);
        update_dialog(p);
        log_step(p, 1, 0, dlg);
        shutdown_dialog(p);
        /* keep only the focus traffic, the rest is covered elsewhere */
        std::vector<std::string> keep;
        for(size_t i = 0; i < log().size(); ++i)
        {
            const std::string &e = log()[i];
            if(e.find("FOCUS") != std::string::npos || e.find("step") == 0 || e.find("trial") == 0)
                keep.push_back(e);
        }
        log().swap(keep);
        check_log(ctx, conf::nm("t%03d", t));
    }
}

CONF_TEST(gui_hotkey_matching)
{
    Session s;
    s.mouse(-100, -100);
    static const int hk[][3] =
    {   /* object key, scancode, ascii */
        { 3, KEY_C, 3 }, { 'A', KEY_A, 'a' }, { 'a', KEY_A, 'A' }, { 13, KEY_ENTER, 13 }, { 27, KEY_ESC, 27 },
        { 9, KEY_TAB, 9 }, { ' ', KEY_SPACE, ' ' }, { '1', KEY_1, '!' }, { 0x4100 | 'a', KEY_A, 'a' },
        { KEY_F1, KEY_F1, 0 }, { '[', KEY_OPENBRACE, '[' }, { '{', KEY_OPENBRACE, '[' }, { 200, 0, 200 },
        { 232, 0, 200 }, { 200, 0, 232 }, { 215, 0, 247 }, { 223, 0, 255 }, { 'z' + 0x100, KEY_Z, 'z' },
        { -1, 0, 0xff }, { 'A', KEY_A, 1 }, { 0, KEY_A, 0 }, { 0, 0, 0 }, { '^', 0, '^' }, { 'Z', KEY_Z, 'z' },
        { '@', 0, '`' }, { 'a', KEY_A, 'a' }, { KEY_F1 << 8, KEY_F1, 0 }, { KEY_A << 8, KEY_A, 0 },
        { KEY_A << 8, KEY_A, 'a' }, { (KEY_A << 8) | 'a', KEY_A, 'a' }, { 0, KEY_F1, 0 }, { KEY_F1, 0, KEY_F1 }
    };
    for(int k = 0; k < (int)(sizeof(hk) / sizeof(hk[0])); ++k)
    {
        for(int v = 0; v < 2; ++v)
        {
            Row r(5);
            r.dlg[2].key = hk[k][0];
            r.dlg[3].key = hk[k][0];
            if(v)
                r.dlg[2].flags |= D_DISABLED;
            log_add(conf::nm("key 0x%x press %d,0x%x variant %d", hk[k][0], hk[k][1], hk[k][2], v));
            Run run(r.dlg, v ? -1 : 1);
            run.upd();
            s.key(hk[k][1], hk[k][2]);
            run.upd();
            run.upd();
            run.fin();
        }
    }
    /* KEY returning flags */
    static const int rets[] = { D_CLOSE, D_REDRAW, D_REDRAWME, D_WANTFOCUS, D_USED_CHAR };
    for(int i = 0; i < 5; ++i)
    {
        Row r(4);
        r.dlg[3].key = 'q';
        r.o[3].ret_once[MSG_KEY] = rets[i];
        log_add(conf::nm("KEY returns 0x%x", rets[i]));
        Run run(r.dlg, 1);
        run.upd();
        s.key(KEY_Q, 'q');
        run.upds(3);
        run.fin();
    }
    check_log(ctx, "log");
}

CONF_TEST(gui_init_focus_mouse)
{
    Session s;
    for(int v = 0; v < 8; ++v)
    {
        Row r(4);
        gui_mouse_focus = (v & 1) ? FALSE : TRUE;
        int focus = (v & 2) ? 1 : -1;
        if(v & 4)
            r.o[2].ret[MSG_WANTFOCUS] = 0;
        s.mouse(55, 15);
        log_add(conf::nm("variant %d", v));
        Run run(r.dlg, focus);
        run.upds(2);
        s.mouse(95, 15);
        run.upd();
        s.mouse(-10, -10);       /* off every object */
        run.upd();
        s.mouse(55, 15);
        run.upd();
        run.fin();
    }
    check_log(ctx, "log");
}

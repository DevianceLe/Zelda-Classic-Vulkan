/*
 * test_gui_engine_joystick.cpp - the dialog manager's joystick input.
 *
 * No joystick driver is installed on either side: the test writes joy[] and
 * num_joysticks itself (poll_joystick() then leaves them alone). In a zcport
 * build without the joystick module these are the GUI's own fallback
 * variables (see src/port/al/gui_joystick.cpp).
 */
#include "gui_conf.h"

#include <string.h>

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

void clear_joy()
{
    memset(&joy[0], 0, sizeof(joy[0]) * 2);
    joy[0].num_sticks = 2;
    joy[0].num_buttons = 4;
    joy[0].stick[0].num_axis = 2;
    joy[0].stick[1].num_axis = 2;
    joy[1] = joy[0];
}

/* what: 0 centre, 1 left, 2 right, 3 up, 4 down, 5 button 0, 6 button 1,
 * 7 button 2, 8 stick 1 right, 9 pos only, 10 joystick 1 right,
 * 11 left+down, 12 right+button, 13 up+down */
void set_joy(int what)
{
    clear_joy();
    JOYSTICK_AXIS_INFO *ax = &joy[0].stick[0].axis[0];
    JOYSTICK_AXIS_INFO *ay = &joy[0].stick[0].axis[1];
    switch(what)
    {
    case 1: ax->d1 = 1; break;
    case 2: ax->d2 = 1; break;
    case 3: ay->d1 = 1; break;
    case 4: ay->d2 = 1; break;
    case 5: joy[0].button[0].b = 1; break;
    case 6: joy[0].button[1].b = 1; break;
    case 7: joy[0].button[2].b = 1; break;
    case 8: joy[0].stick[1].axis[0].d2 = 1; break;
    case 9: ax->pos = 127; ay->pos = -128; break;
    case 10: joy[1].stick[0].axis[0].d2 = 1; break;
    case 11: ax->d1 = 1; ay->d2 = 1; break;
    case 12: ax->d2 = 1; joy[0].button[0].b = 1; break;
    case 13: ay->d1 = 1; ay->d2 = 1; break;
    }
}

}

CONF_TEST(gui_joystick_keys)
{
    Session s;
    s.mouse(-100, -100);
    for(int what = 1; what <= 13; ++what)
    {
        for(int nj = 0; nj < 3; ++nj)
        {
            num_joysticks = nj;
            Row r(6);
            log_add(conf::nm("joystick input %d, %d joysticks", what, nj));
            Run run(r.dlg, 2);
            run.upd();
            set_joy(what);
            s.key(KEY_X, 'x');
            run.upd();                  /* joystick first, the key waits */
            run.upd();                  /* still pressed: ignored */
            set_joy(0);
            run.upd();                  /* centred */
            set_joy(what);
            run.upd();                  /* again */
            set_joy(0);
            run.upd();
            input().keys.clear();
            run.fin();
        }
    }
    num_joysticks = 0;
    clear_joy();
    check_log(ctx, "log");
}

CONF_TEST(gui_joystick_joy_on)
{
    Session s;
    s.mouse(-100, -100);
    num_joysticks = 1;
    Row r(5);
    Run run(r.dlg, 1);
    log_add(conf::nm("joy_on after init %d", run.p->joy_on));
    set_joy(2);                         /* held when the dialog starts */
    run.upd();
    log_add(conf::nm("joy_on %d", run.p->joy_on));
    run.upd();
    set_joy(0);
    run.upd();
    log_add(conf::nm("joy_on %d", run.p->joy_on));
    /* the game sets joy_on to swallow the button it just used */
    run.p->joy_on = TRUE;
    set_joy(5);
    run.upd();
    log_add(conf::nm("joy_on %d", run.p->joy_on));
    set_joy(0);
    run.upd();
    set_joy(5);
    run.upd();
    log_add(conf::nm("joy_on %d", run.p->joy_on));
    set_joy(0);
    run.upd();
    run.fin();
    num_joysticks = 0;
    clear_joy();
    check_log(ctx, "log");
}

CONF_TEST(gui_joystick_during_click)
{
    Session s;
    num_joysticks = 1;
    Row r(5);
    s.mouse(55, 15);
    Run run(r.dlg, -1);
    run.upd();
    s.button(1);
    set_joy(2);
    run.upd();                          /* click: the joystick waits */
    sleep_ms(40);
    s.button(0);
    sleep_ms(40);
    run.upd();                          /* waiting for a double click */
    run.settle();
    run.upd();
    set_joy(0);
    run.upd();
    run.fin();
    num_joysticks = 0;
    clear_joy();
    check_log(ctx, "log");
}

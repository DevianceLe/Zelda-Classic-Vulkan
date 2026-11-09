/*
 * gui_conf.h - helpers for the GUI conformance tests (test_gui_*.cpp).
 *
 * Everything here uses only the public Allegro API, so it compiles against
 * the reference headers and against zcport alike:
 *
 *   - a scripted input source: the gui_mouse_x/y/z/b hooks and
 *     install_keyboard_hooks() read from a script that the test advances one
 *     step at a time (no real devices, no timing);
 *   - a logging dialog procedure (log_proc) that records every message it
 *     gets (object id, message, c, flags) and returns a configurable value;
 *   - a GUI screen (memory bitmap given to gui_set_screen()).
 *
 * The message log is checked as a conf::Seq, so a failure shows the first
 * message that differs.
 *
 * C++98 on purpose (like conf.h).
 */
#ifndef ZC_CONFORMANCE_GUI_CONF_H
#define ZC_CONFORMANCE_GUI_CONF_H

#include "conf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

/* The reference needs a real system driver for install_keyboard_hooks() and
 * the timers its dialog manager installs (with SYSTEM_NONE both crash inside
 * alleg44.dll). The port runs with SYSTEM_NONE: its GUI needs no driver.
 * This only changes the setup, never what a check prints. */
#ifdef CONF_SIDE_REF
static conf::DefaultSystem guiconf_ref_system_(SYSTEM_AUTODETECT);
#endif

namespace guiconf
{

/* ------------------------------------------------------------------------- */
/* message log                                                              */
/* ------------------------------------------------------------------------- */

inline std::vector<std::string> &log()
{
    static std::vector<std::string> l;
    return l;
}

inline void log_add(const std::string &s)
{
    log().push_back(s);
}

inline const char *msg_name(int msg)
{
    static const char *const names[] =
    {
        "MSG_0", "START", "END", "DRAW", "CLICK", "DCLICK", "KEY", "CHAR",
        "UCHAR", "XCHAR", "WANTFOCUS", "GOTFOCUS", "LOSTFOCUS", "GOTMOUSE",
        "LOSTMOUSE", "IDLE", "RADIO", "WHEEL", "LPRESS", "LRELEASE", "MPRESS",
        "MRELEASE", "RPRESS", "RRELEASE", "WANTMOUSE"
    };

    if(msg >= 0 && msg < (int)(sizeof(names) / sizeof(names[0])))
        return names[msg];

    static char buf[32];
    sprintf(buf, "MSG_%d", msg);
    return buf;
}

/* D_* flags as letters: X exit, S selected, F gotfocus, M gotmouse,
 * H hidden, D disabled, Y dirty, I internal, plus "+0x.." for user bits. */
inline std::string flag_text(int f)
{
    std::string s;
    if(f & D_EXIT) s += 'X';
    if(f & D_SELECTED) s += 'S';
    if(f & D_GOTFOCUS) s += 'F';
    if(f & D_GOTMOUSE) s += 'M';
    if(f & D_HIDDEN) s += 'H';
    if(f & D_DISABLED) s += 'D';
    if(f & D_DIRTY) s += 'Y';
    if(f & D_INTERNAL) s += 'I';
    if(f & ~(D_USER - 1))
    {
        char buf[32];
        sprintf(buf, "+0x%x", (unsigned)(f & ~(D_USER - 1)));
        s += buf;
    }
    if(s.empty())
        s = "-";
    return s;
}

/* Prints the log as one sequence check and clears it. */
inline void check_log(conf::Ctx &ctx, const char *c)
{
    conf::Seq s(ctx, c);
    std::vector<std::string> &l = log();

    /* GUICONF_TRACE=1: also print the log (for writing tests). */
    bool trace = getenv("GUICONF_TRACE") != 0;

    for(size_t i = 0; i < l.size(); ++i)
    {
        if(trace)
        {
            printf("LOG %s\n", l[i].c_str());
            fflush(stdout);
        }
        s.add_str(l[i].c_str(), "entry %d", (int)i);
    }

    s.end();
    l.clear();
}

/* ------------------------------------------------------------------------- */
/* scripted input                                                             */
/* ------------------------------------------------------------------------- */

struct Input
{
    int mx, my, mz, mb;
    std::vector<int> keys;        /* readkey() values */
    int kb_calls, rk_calls;       /* how often the hooks were called */
    int mouse_calls;
};

inline Input &input()
{
    static Input in;
    return in;
}

inline int hook_mouse_x() { ++input().mouse_calls; return input().mx; }
inline int hook_mouse_y() { ++input().mouse_calls; return input().my; }
inline int hook_mouse_z() { ++input().mouse_calls; return input().mz; }
inline int hook_mouse_b() { ++input().mouse_calls; return input().mb; }

inline int hook_keypressed()
{
    ++input().kb_calls;
    return input().keys.empty() ? FALSE : TRUE;
}

inline int hook_readkey()
{
    ++input().rk_calls;
    Input &in = input();

    if(in.keys.empty())
        return 0;

    int k = in.keys.front();
    in.keys.erase(in.keys.begin());
    return k;
}

/* Saved GUI state, restored by Session's destructor. */
struct Session
{
    int (*old_mx)(void);
    int (*old_my)(void);
    int (*old_mz)(void);
    int (*old_mb)(void);
    int old_focus;
    int old_fg, old_mg, old_bg, old_baseline;
    int old_shifts;
    BITMAP *scr;
    BITMAP *old_screen;

    /* w x h 8-bit memory bitmap as the GUI screen, scripted mouse and
     * keyboard. */
    Session(int w = 320, int h = 240, int depth = 8)
    {
        old_mx = gui_mouse_x;
        old_my = gui_mouse_y;
        old_mz = gui_mouse_z;
        old_mb = gui_mouse_b;
        old_focus = gui_mouse_focus;
        old_fg = gui_fg_color;
        old_mg = gui_mg_color;
        old_bg = gui_bg_color;
        old_baseline = gui_font_baseline;
        old_shifts = key_shifts;

        Input &in = input();
        in.mx = in.my = in.mz = in.mb = 0;
        in.keys.clear();
        in.kb_calls = in.rk_calls = in.mouse_calls = 0;

        gui_mouse_x = hook_mouse_x;
        gui_mouse_y = hook_mouse_y;
        gui_mouse_z = hook_mouse_z;
        gui_mouse_b = hook_mouse_b;
        install_keyboard_hooks(hook_keypressed, hook_readkey);

        scr = create_bitmap_ex(depth, w, h);
        clear_to_color(scr, 0);
        gui_set_screen(scr);
        /* The reference dialog manager needs 'screen' (it compares it with
         * the GUI bitmap); the game always has one. */
        old_screen = screen;
        screen = scr;
        log().clear();
    }

    ~Session()
    {
        gui_set_screen(NULL);
        screen = old_screen;
        destroy_bitmap(scr);
        install_keyboard_hooks(NULL, NULL);
        gui_mouse_x = old_mx;
        gui_mouse_y = old_my;
        gui_mouse_z = old_mz;
        gui_mouse_b = old_mb;
        gui_mouse_focus = old_focus;
        gui_fg_color = old_fg;
        gui_mg_color = old_mg;
        gui_bg_color = old_bg;
        gui_font_baseline = old_baseline;
        key_shifts = old_shifts;
        input().keys.clear();
        log().clear();
    }

    void mouse(int x, int y, int b = 0)
    {
        input().mx = x;
        input().my = y;
        input().mb = b;
    }
    void wheel(int z) { input().mz = z; }
    void button(int b) { input().mb = b; }
    /* readkey() format: (scancode << 8) | ascii. */
    void key(int scancode, int ascii = 0) { input().keys.push_back((scancode << 8) | (ascii & 0xFF)); }
    void shifts(int s) { key_shifts = s; }
};

/* Real time passes (the double-click detection of the dialog manager is
 * timed). */
inline void sleep_ms(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

/* After a mouse click the dialog manager waits for a double click before it
 * goes back to normal processing; this is comfortably longer. */
const int DCLICK_SETTLE_MS = 450;

/* ------------------------------------------------------------------------- */
/* the logging dialog procedure                                               */
/* ------------------------------------------------------------------------- */

/* Behaviour of one log_proc object; d->dp points to it. */
struct Obj
{
    int id;                      /* printed in the log */
    int ret[MSG_USER + 8];       /* return value per message */
    int ret_once[MSG_USER + 8];  /* if nonzero: returned once, then ret[] */
    bool used_char;              /* MSG_CHAR/UCHAR/XCHAR return D_USED_CHAR */
    int (*extra)(int msg, DIALOG *d, int c);   /* called first if set; its
                                                  nonnegative result wins */

    explicit Obj(int id_ = 0)
    {
        id = id_;
        for(int i = 0; i < MSG_USER + 8; ++i)
            ret[i] = ret_once[i] = 0;
        used_char = false;
        extra = 0;
    }
};

inline int log_proc(int msg, DIALOG *d, int c)
{
    Obj *o = (Obj *)d->dp;
    char buf[128];
    int id = o ? o->id : -1;

    if(msg == MSG_CHAR || msg == MSG_UCHAR || msg == MSG_XCHAR || msg == MSG_RADIO
            || msg == MSG_WHEEL || msg >= MSG_USER)
        sprintf(buf, "%d %s c=0x%x f=%s", id, msg_name(msg), (unsigned)c, flag_text(d->flags).c_str());
    else if(c != 0)
        sprintf(buf, "%d %s c=%d f=%s", id, msg_name(msg), c, flag_text(d->flags).c_str());
    else
        sprintf(buf, "%d %s f=%s", id, msg_name(msg), flag_text(d->flags).c_str());

    size_t idx = log().size();
    log_add(buf);

    int r = 0;
    bool have = false;

    if(o && o->extra)
    {
        int e = o->extra(msg, d, c);
        if(e >= 0)
        {
            r = e;
            have = true;
        }
    }

    if(!have && o && msg >= 0 && msg < MSG_USER + 8)
    {
        if(o->ret_once[msg])
        {
            r = o->ret_once[msg];
            o->ret_once[msg] = 0;
        }
        else
            r = o->ret[msg];

        if(o->used_char && (msg == MSG_CHAR || msg == MSG_UCHAR || msg == MSG_XCHAR))
            r |= D_USED_CHAR;
    }

    if(r)
    {
        sprintf(buf, " -> 0x%x", (unsigned)r);
        if(idx == log().size() - 1)
            log()[idx] += buf;
        else
        {
            char b2[160];
            sprintf(b2, "%d ret %s%s", id, msg_name(msg), buf);
            log_add(b2);
        }
    }

    return r;
}

/* A DIALOG entry for log_proc. */
inline DIALOG obj(Obj *o, int x, int y, int w, int h, int flags = 0, int key = 0)
{
    DIALOG d;
    memset(&d, 0, sizeof(d));
    d.proc = log_proc;
    d.x = x;
    d.y = y;
    d.w = w;
    d.h = h;
    d.fg = 255;
    d.bg = 0;
    d.key = key;
    d.flags = flags;
    d.dp = o;
    return d;
}

inline DIALOG end_obj()
{
    DIALOG d;
    memset(&d, 0, sizeof(d));
    return d;
}

inline DIALOG proc_obj(DIALOG_PROC p, int x, int y, int w, int h, int fg, int bg,
                       int key, int flags, int d1, int d2, void *dp = 0, void *dp2 = 0, void *dp3 = 0)
{
    DIALOG d;
    d.proc = p;
    d.x = x;
    d.y = y;
    d.w = w;
    d.h = h;
    d.fg = fg;
    d.bg = bg;
    d.key = key;
    d.flags = flags;
    d.d1 = d1;
    d.d2 = d2;
    d.dp = dp;
    d.dp2 = dp2;
    d.dp3 = dp3;
    return d;
}

/* The player fields a game can observe, as text. */
inline std::string player_text(DIALOG_PLAYER *p)
{
    char buf[160];
    if(!p)
        return "(null)";
    sprintf(buf, "obj=%d focus=%d mouse=%d", p->obj, p->focus_obj, p->mouse_obj);
    return buf;
}

/* Logs "step N: ret=.. obj=.. focus=.. mouse=.." and the dialog flags. */
inline void log_step(DIALOG_PLAYER *p, int step, int ret, DIALOG *dlg)
{
    char buf[200];
    sprintf(buf, "step %d ret=%d %s", step, ret, player_text(p).c_str());
    std::string s = buf;
    if(dlg)
    {
        s += " flags";
        for(int i = 0; dlg[i].proc; ++i)
        {
            s += ' ';
            s += flag_text(dlg[i].flags);
        }
    }
    log_add(s);
}

/* Drives a dialog step by step and logs each step. */
struct Run
{
    DIALOG *dlg;
    DIALOG_PLAYER *p;
    int step;
    bool done;

    Run(DIALOG *d, int focus) : dlg(d), p(0), step(0), done(false)
    {
        char buf[64];
        sprintf(buf, "init focus=%d", focus);
        log_add(buf);
        p = init_dialog(d, focus);
        log_step(p, 0, -99, dlg);
    }

    /* One update_dialog(); returns its result. */
    int upd()
    {
        if(done)
            return 0;
        int r = update_dialog(p);
        log_step(p, ++step, r, dlg);
        return r;
    }

    /* n updates (stops early if the dialog closed). */
    int upds(int n)
    {
        int r = 1;
        for(int i = 0; i < n && r; ++i)
            r = upd();
        return r;
    }

    /* Let the double-click window pass, then one update. */
    int settle()
    {
        sleep_ms(DCLICK_SETTLE_MS);
        return upd();
    }

    int fin()
    {
        int r = shutdown_dialog(p);
        char buf[64];
        sprintf(buf, "shutdown %d", r);
        log_add(buf);
        p = 0;
        done = true;
        return r;
    }
};

} /* namespace guiconf */

#endif /* ZC_CONFORMANCE_GUI_CONF_H */

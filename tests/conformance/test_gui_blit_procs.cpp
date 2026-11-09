/*
 * test_gui_blit_procs.cpp - the dialog procedures that draw bitmaps
 * (d_bitmap_proc, d_icon_proc) and popup_dialog().
 *
 * The port side needs the blit module (blit, stretch_blit), so it links once
 * that is merged.
 */
#include "gui_conf.h"

#include <string.h>

using namespace guiconf;

namespace
{

struct Images
{
    BITMAP *img[3];
    Images()
    {
        for(int i = 0; i < 3; ++i)
        {
            img[i] = create_bitmap_ex(8, 10 + i * 3, 8 + i * 2);
            for(int y = 0; y < img[i]->h; ++y)
                for(int x = 0; x < img[i]->w; ++x)
                    putpixel(img[i], x, y, 10 + i * 5 + ((x + y) % 4));
        }
    }
    ~Images()
    {
        for(int i = 0; i < 3; ++i)
            destroy_bitmap(img[i]);
    }
};

}

CONF_TEST(gui_bitmap_icon_draw)
{
    Session s(40, 30);
    BITMAP *other = create_bitmap_ex(8, 40, 30);
    screen = other;
    Images im;
    conf::Rng &rng = ctx.rng;
    static const int flagsv[] =
    {
        0, D_SELECTED, D_DISABLED, D_GOTFOCUS, D_SELECTED | D_GOTFOCUS, D_DISABLED | D_SELECTED,
        D_EXIT | D_SELECTED | D_GOTFOCUS, D_EXIT | D_GOTFOCUS
    };
    for(int pi = 0; pi < 2; ++pi)
    {
        conf::Seq q(ctx, pi ? "icon" : "bitmap");
        for(int t = 0; t < 400; ++t)
        {
            clear_to_color(s.scr, 3);
            clear_to_color(other, 4);
            DIALOG d = proc_obj(pi ? d_icon_proc : d_bitmap_proc, rng.range(-4, 20), rng.range(-4, 16),
                                rng.range(0, 24), rng.range(0, 20), 31, 33, 0, flagsv[rng.range(0, 7)],
                                rng.range(0, 4), rng.range(0, 4), im.img[rng.range(0, 2)],
                                rng.range(0, 1) ? im.img[1] : 0, rng.range(0, 1) ? im.img[2] : 0);
            int r = d.proc(MSG_DRAW, &d, 0);
            q.add_int(r, "ret case %d", t);
            q.add_bitmap(s.scr, "gui case %d (%d,%d,%d,%d flags %s d1 %d d2 %d)", t, d.x, d.y, d.w, d.h,
                         flag_text(d.flags).c_str(), d.d1, d.d2);
            q.add_bitmap(other, "screen case %d", t);
        }
        q.end();
    }
    screen = s.scr;
    destroy_bitmap(other);
}

/* every message, with a mouse held for a number of gui_mouse_b() calls of
 * this thread (the reference's double click timer polls from another) */
namespace
{
    int s_bcalls, s_hold, s_move, s_x1, s_y1, s_x2, s_y2, s_b;
    std::thread::id s_main;
    int sm_x() { return s_bcalls >= s_move ? s_x2 : s_x1; }
    int sm_y() { return s_bcalls >= s_move ? s_y2 : s_y1; }
    int sm_z() { return 0; }
    int sm_b()
    {
        if(std::this_thread::get_id() == s_main)
            ++s_bcalls;
        return s_bcalls <= s_hold ? s_b : 0;
    }

    std::string run_message(BITMAP *scr, BITMAP *img, DIALOG_PROC p, int msg, int flags, int hold, int move,
                            int in1, int in2, int b)
    {
        Obj o0(0), o3(3);
        DIALOG dlg[4];
        dlg[0] = obj(&o0, 0, 0, 64, 40);
        dlg[1] = proc_obj(p, 4, 4, 12, 10, 31, 33, 'a', flags, 0, 0, img);
        dlg[2] = obj(&o3, 40, 4, 10, 10);
        dlg[3] = end_obj();
        clear_to_color(scr, 3);
        s_x1 = in1 ? 6 : 50;
        s_y1 = in1 ? 6 : 30;
        s_x2 = in2 ? 8 : 60;
        s_y2 = in2 ? 7 : 35;
        s_hold = hold;
        s_move = move;
        s_b = b;
        s_bcalls = 0;
        DIALOG_PLAYER *pl = init_dialog(dlg, -1);
        set_clip_rect(scr, 0, 0, scr->w - 1, scr->h - 1);   /* no graphics mode: init_dialog emptied it */
        log().clear();
        s_bcalls = 0;
        int r = dlg[1].proc(msg, &dlg[1], 0);
        std::string out = conf::nm("ret 0x%x flags %s %s | ", r, flag_text(dlg[1].flags).c_str(),
                                   conf::hash_text(conf::hash_bitmap(scr)).c_str());
        for(size_t i = 0; i < log().size(); ++i)
            out += log()[i] + "; ";
        log().clear();
        shutdown_dialog(pl);
        log().clear();
        return out;
    }
}

CONF_TEST(gui_bitmap_icon_messages)
{
    Session s(64, 40);
    s_main = std::this_thread::get_id();
    gui_mouse_x = sm_x;
    gui_mouse_y = sm_y;
    gui_mouse_z = sm_z;
    gui_mouse_b = sm_b;
    Images im;
    static const int flagsv[] = { 0, D_SELECTED, D_EXIT, D_EXIT | D_SELECTED, D_DISABLED, D_GOTFOCUS };
    for(int pi = 0; pi < 2; ++pi)
    {
        conf::Seq q(ctx, pi ? "icon" : "bitmap");
        for(int msg = 1; msg <= MSG_USER; ++msg)
            for(int f = 0; f < 6; ++f)
                for(int v = 0; v < (msg == MSG_CLICK ? 12 : 2); ++v)
                {
                    int hold = (v % 4) * 3, move = (v / 4) * 2;
                    int in1 = (v & 1) | (msg != MSG_CLICK), in2 = (v / 2) & 1;
                    std::string r = run_message(s.scr, im.img[0], pi ? d_icon_proc : d_bitmap_proc, msg, flagsv[f],
                                                hold, move, in1, in2, 1 + (v & 2));
                    q.add_str(r.c_str(), "%s flags %s variant %d", msg_name(msg), flag_text(flagsv[f]).c_str(), v);
                }
        q.end();
    }
}

/* popup_dialog(): what is under the first object comes back afterwards.
 * The scribbling object draws over everything when the dialog ends (it
 * resets the clip rectangle that init_dialog emptied), and can move the
 * dialog. */
namespace
{
    int s_move_dialog;

    int scribble_proc(int msg, DIALOG *d, int c)
    {
        (void)c;
        if(msg == MSG_IDLE)
            return D_CLOSE;
        if(msg == MSG_END)
        {
            BITMAP *g = gui_get_screen();
            set_clip_rect(g, 0, 0, g->w - 1, g->h - 1);
            rectfill(g, 0, 0, g->w - 1, g->h - 1, 9);
            if(s_move_dialog)
            {
                d->x += 3;
                d->y += 2;
            }
        }
        return D_O_K;
    }
}

CONF_TEST(gui_popup_dialog)
{
    Session s(40, 30);
    for(int v = 0; v < 8; ++v)
    {
        BITMAP *g = create_bitmap_ex(8, 40, 30);
        for(int y = 0; y < 30; ++y)
            for(int x = 0; x < 40; ++x)
                putpixel(g, x, y, (x * 3 + y * 7) % 50);
        gui_set_screen(g);
        screen = (v & 4) ? s.scr : g;
        /* moving a dialog that sticks out of the bitmap would put back
         * pixels the saved copy never got (undefined: not tested) */
        s_move_dialog = (v & 1) && !(v & 2);
        DIALOG dlg[3];
        dlg[0] = proc_obj(scribble_proc, (v & 2) ? -3 : 5, (v & 2) ? 20 : 4, 20, 14, 0, 0, 0, 0, 0, 0);
        dlg[1] = proc_obj(d_box_proc, 6, 5, 4, 4, 1, 2, 0, 0, 0, 0);
        dlg[2] = end_obj();
        ctx.check_int(conf::nm("ret%d", v), popup_dialog(dlg, -1));
        ctx.check_bitmap(conf::nm("result%d", v), g);
        screen = s.scr;
        gui_set_screen(s.scr);
        destroy_bitmap(g);
    }
}

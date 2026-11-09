/*
 * test_gui_text_procs.cpp - gui_textout_ex(), gui_strlen() and the standard
 * dialog procedures that draw text or use makecol(): d_text_proc,
 * d_ctext_proc, d_rtext_proc, d_button_proc, d_check_proc, d_radio_proc,
 * d_shadow_box_proc. Drawing with the game's fonts (fonts.dat), and the
 * reaction to every message, including mouse tracking with a scripted mouse.
 *
 * The port side needs the text and colour modules (text_length, textout_ex,
 * font, makecol), so it links once those are merged.
 */
#include "gui_conf.h"
#include "conf_zc.h"
#include "../../src/fontsdat.h"

#include <string.h>

using namespace guiconf;

namespace
{

DATAFILE *load_fonts(conf::Ctx &ctx)
{
    if(!ctx.have_data("fonts.dat"))
        return 0;
    packfile_password(conf::zc::datapwd());
    DATAFILE *fd = load_datafile(ctx.data("fonts.dat").c_str());
    packfile_password(NULL);
    return fd;
}

const int k_fonts[] = { FONT_NES, FONT_6x6, FONT_GUI_PROP, FONT_12x8, FONT_6x4 };
const char *const k_texts[] = { "A", "AB&C", "&&x", "", "x&", "Wide text&!", "&a&b" };
const int k_flags[] =
{
    0, D_SELECTED, D_GOTFOCUS, D_GOTFOCUS | D_SELECTED, D_DISABLED, D_DISABLED | D_SELECTED | D_GOTFOCUS,
    D_EXIT | D_GOTFOCUS | D_SELECTED, D_EXIT | D_GOTFOCUS
};

struct ProcInfo
{
    const char *name;
    DIALOG_PROC proc;
};

const ProcInfo k_procs[] =
{
    { "text", d_text_proc }, { "ctext", d_ctext_proc }, { "rtext", d_rtext_proc }, { "button", d_button_proc },
    { "check", d_check_proc }, { "radio", d_radio_proc }, { "shadow", d_shadow_box_proc }
};

}

CONF_TEST(gui_textout)
{
    DATAFILE *fd = load_fonts(ctx);
    if(!fd)
    {
        ctx.skip("fonts.dat not found");
        return;
    }
    FONT *old = font;
    BITMAP *bmp = create_bitmap_ex(8, 96, 24);
    conf::Seq q(ctx, "textout");
    for(int fi = 0; fi < 5; ++fi)
    {
        font = (FONT *)fd[k_fonts[fi]].dat;
        for(int t = 0; t < 7; ++t)
            for(int v = 0; v < 6; ++v)
            {
                clear_to_color(bmp, 3);
                gui_font_baseline = (v >= 4) ? v - 2 : 0;
                int r = gui_textout_ex(bmp, k_texts[t], 40 + v, 3 + v, 200, (v & 1) ? 17 : -1, (v & 2) ? TRUE : FALSE);
                q.add_int(r, "ret font %d text %d v %d", k_fonts[fi], t, v);
                q.add_int(gui_strlen(k_texts[t]), "strlen font %d text %d", k_fonts[fi], t);
                q.add_bitmap(bmp, "bmp font %d text %d v %d", k_fonts[fi], t, v);
            }
        /* NULL bitmap: only measures */
        q.add_int(gui_textout_ex(NULL, "AB&C", 0, 0, 1, -1, TRUE), "null bmp font %d", k_fonts[fi]);
    }
    q.end();
    gui_font_baseline = 0;
    font = old;
    destroy_bitmap(bmp);
    unload_datafile(fd);
}

CONF_TEST(gui_text_procs_draw)
{
    DATAFILE *fd = load_fonts(ctx);
    if(!fd)
    {
        ctx.skip("fonts.dat not found");
        return;
    }
    Session s(48, 24);
    BITMAP *other = create_bitmap_ex(8, 48, 24);
    screen = other;                      /* procs draw on the GUI bitmap */
    FONT *old = font;
    gui_mg_color = 99;
    conf::Rng &rng = ctx.rng;
    for(int fi = 0; fi < 5; ++fi)
    {
        font = (FONT *)fd[k_fonts[fi]].dat;
        for(int pi = 0; pi < 7; ++pi)
        {
            conf::Seq q(ctx, conf::nm("%s_font%d", k_procs[pi].name, k_fonts[fi]));
            for(int t = 0; t < 40; ++t)
            {
                clear_to_color(s.scr, 3);
                clear_to_color(other, 4);
                int x = rng.range(-3, 20), y = rng.range(-3, 12), w = rng.range(0, 40), h = rng.range(0, 20);
                DIALOG d = proc_obj(k_procs[pi].proc, x, y, w, h, 200, 17, 0, k_flags[rng.range(0, 7)],
                                    rng.range(0, 2), rng.range(0, 2), (void *)k_texts[rng.range(0, 6)],
                                    (t & 1) ? fd[FONT_6x6].dat : 0);
                gui_font_baseline = (t % 5 == 0) ? 2 : (t % 7 == 0) ? 3 : 0;
                int r = d.proc(MSG_DRAW, &d, 0);
                q.add_int(r, "ret case %d", t);
                q.add_int(d.flags, "flags case %d", t);
                q.add_bitmap(s.scr, "gui case %d (%d,%d,%d,%d)", t, x, y, w, h);
                q.add_bitmap(other, "screen case %d", t);
            }
            q.end();
        }
    }
    gui_font_baseline = 0;
    font = old;
    screen = s.scr;
    destroy_bitmap(other);
    unload_datafile(fd);
}

/* ---- messages, with a mouse that is held for a number of gui_mouse_b()
 * calls and moves after a number of them. Only calls from the test's own
 * thread count: the reference's double click timer polls gui_mouse_b()
 * from another thread. ---- */
namespace
{
    int s_bcalls, s_hold, s_move, s_x1, s_y1, s_x2, s_y2, s_b;
    std::thread::id s_main;
    int sm_x() { return s_bcalls >= s_move ? s_x2 : s_x1; }
    int sm_y() { return s_bcalls >= s_move ? s_y2 : s_y1; }
    int sm_z() { return 0; }
    int sm_b()
    {
        if(std::this_thread::get_id() != s_main)
            return s_bcalls <= s_hold ? s_b : 0;
        ++s_bcalls;
        return s_bcalls <= s_hold ? s_b : 0;
    }

    std::string run_message(BITMAP *scr, DIALOG_PROC proc, int msg, int c, int flags, int d1, int flags2,
                            int hold, int move, int inside1, int inside2, int b)
    {
        Obj o0(0), o3(3);
        DIALOG dlg[5];
        dlg[0] = obj(&o0, 0, 0, 64, 40);
        dlg[1] = proc_obj(proc, 4, 4, 30, 12, 200, 17, 'a', flags, d1, 0, (void *)"A&b");
        dlg[2] = proc_obj(proc, 4, 20, 30, 12, 200, 17, 'b', flags2, d1, 0, (void *)"C");
        dlg[3] = obj(&o3, 40, 4, 10, 10);
        dlg[4] = end_obj();
        clear_to_color(scr, 3);
        s_x1 = inside1 ? 10 : 50;
        s_y1 = inside1 ? 8 : 30;
        s_x2 = inside2 ? 12 : 60;
        s_y2 = inside2 ? 9 : 35;
        s_hold = hold;
        s_move = move;
        s_b = b;
        s_bcalls = 0;
        DIALOG_PLAYER *pl = init_dialog(dlg, -1);
        set_clip_rect(scr, 0, 0, scr->w - 1, scr->h - 1);   /* no graphics mode: init_dialog emptied it */
        log().clear();
        s_bcalls = 0;
        int r = dlg[1].proc(msg, &dlg[1], c);
        std::string out = conf::nm("ret 0x%x flags %s %s %s | ", r, flag_text(dlg[1].flags).c_str(),
                                   flag_text(dlg[2].flags).c_str(), conf::hash_text(conf::hash_bitmap(scr)).c_str());
        for(size_t i = 0; i < log().size(); ++i)
            out += log()[i] + "; ";
        log().clear();
        shutdown_dialog(pl);
        log().clear();
        return out;
    }
}

CONF_TEST(gui_text_procs_messages)
{
    DATAFILE *fd = load_fonts(ctx);
    if(!fd)
    {
        ctx.skip("fonts.dat not found");
        return;
    }
    Session s(64, 40);
    s_main = std::this_thread::get_id();
    gui_mouse_x = sm_x;
    gui_mouse_y = sm_y;
    gui_mouse_z = sm_z;
    gui_mouse_b = sm_b;
    FONT *old = font;
    font = (FONT *)fd[FONT_NES].dat;
    static const int flagsv[] = { 0, D_SELECTED, D_EXIT, D_EXIT | D_SELECTED, D_DISABLED, D_GOTFOCUS };
    for(int pi = 0; pi < 7; ++pi)
    {
        conf::Seq q(ctx, k_procs[pi].name);
        for(int msg = 1; msg <= MSG_USER; ++msg)
        {
            if(msg == MSG_DRAW)
                continue;
            for(int f = 0; f < 6; ++f)
            {
                for(int v = 0; v < (msg == MSG_CLICK ? 12 : 2); ++v)
                {
                    int hold = (v % 4) * 3, move = (v / 4) * 2;
                    int in1 = (v & 1) | (msg != MSG_CLICK), in2 = (v / 2) & 1;
                    int c = (msg == MSG_RADIO) ? v : (msg == MSG_CHAR || msg == MSG_XCHAR) ? 0x1e61 : 0;
                    std::string r = run_message(s.scr, k_procs[pi].proc, msg, c, flagsv[f], v & 1,
                                                (v & 1) ? D_SELECTED : 0, hold, move, in1, in2, 1 + (v & 2));
                    q.add_str(r.c_str(), "%s flags %s variant %d", msg_name(msg), flag_text(flagsv[f]).c_str(), v);
                }
            }
        }
        q.end();
    }
    font = old;
    unload_datafile(fd);
}

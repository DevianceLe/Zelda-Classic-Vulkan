// 2.55/3.0 script commands and registers that are whole game features or plain ports of
// upstream's ffscript.cpp and scripting/types/*.cpp: save menus, portals, websockets (as
// upstream with no network to reach), subscreen objects made and changed by scripts, the
// wipes, sounds, text measures, name lookups and the rest (item->Explode: q30_scripts_live.h). Included once, at the end of
// src/ffscript.cpp, after q30_scripts_cmds.h (it uses that file's helpers); run_script reaches
// it through zasm30_command (zasm30_sys_command) and the register dispatch (zasm30_sys_get/_set).

#ifndef ZC_Q30_SCRIPTS_SYS_H
#define ZC_Q30_SCRIPTS_SYS_H

#include "q30_savemenu.h"

extern bool button_hold[18];
bool q30_should_show_time();    // zc_subscr.cpp
extern int skipcont;
extern bool Playing;
void zapout();
void zapin();
int count_layer_enemies();
int q30_summon_layered_enemies(enemy *e, int count, int limit, bool random_count);    // guys.cpp
bool q30_switchhook_script(int style, int pos, enemy *e, sprite *s);   // q30_triggers.cpp
void q30_mirror_portal_remove(bool saved);
bool q30_portal_warp(int x, int y, int dmap, int scr, int effect, int sound);
bool q30_region_trigger_secrets(int scr);                               // q30_regions.cpp
mapscr *q30_region_tmp_screen(int index, int layer);
int q30_currspal6 = -1, q30_currspal14 = -1;    // pal.cpp's loadpalset keeps them

// ==== save menus (upstream save_menu.cpp, scripting/types/savemenu.cpp) =======================

static Q30SaveMenu zasm30_save_menus[Q30_NUM_SAVE_MENUS];
static int zasm30_savemenu_game_over = 0, zasm30_savemenu_f6 = 0;

void q30_savemenus_clear()
{
    for(int q = 0; q < Q30_NUM_SAVE_MENUS; ++q)
        zasm30_save_menus[q] = Q30SaveMenu();
}

Q30SaveMenu *q30_savemenu_slot(int index)
{
    return (index >= 0 && index < Q30_NUM_SAVE_MENUS) ? &zasm30_save_menus[index] : NULL;
}

void q30_savemenu_set_special(int game_over, int f6)
{
    zasm30_savemenu_game_over = game_over;
    zasm30_savemenu_f6 = f6;
}

static Q30SaveMenu *z30_savemenu(long ref)
{
    if(unsigned(ref - 1) < (unsigned)Q30_NUM_SAVE_MENUS)
        return &zasm30_save_menus[ref - 1];

    Z_scripterrlog("Invalid save_menu using UID = %ld\n", ref);
    return NULL;
}

static bool z30_savemenu_valid(const Q30SaveMenu &m) { return !m.options.empty(); }

// overtileblock16: a block of w x h tiles (rows of 20 tiles in the tile sheet)
static void z30_tile_block(BITMAP *dest, long tile, int x, int y, int w, int h, int cset)
{
    for(int j = 0; j < h; ++j)
        for(int i = 0; i < w; ++i)
            overtile16(dest, (int)(tile + j * TILES_PER_ROW + i), x + i * 16, y + j * 16, cset, 0);
}

static void z30_savemenu_draw(const Q30SaveMenu &m, BITMAP *dest, int cursor, int clk)
{
    const int TARG_WID = 256, TARG_HEI = 236;
    int cl = dest->cl, ct = dest->ct, cr = dest->cr, cb = dest->cb;
    set_clip_rect(dest, 0, 0, TARG_WID, TARG_HEI);
    clear_to_color(dest, m.bg_color);

    if(m.bg_tile)
    {
        if(m.flags & Q30_SMENU_REPEAT_BG)
        {
            for(int t_y = 0; t_y < TARG_HEI; t_y += 16 * m.bg_th)
                for(int t_x = 0; t_x < TARG_WID; t_x += 16 * m.bg_tw)
                    z30_tile_block(dest, m.bg_tile, t_x, t_y, m.bg_tw, m.bg_th, m.bg_cset);
        }
        else
            z30_tile_block(dest, m.bg_tile, 0, 0, m.bg_tw, m.bg_th, m.bg_cset);
    }

    for(size_t q = 0; q < m.misc_texts.size(); ++q)
    {
        const Q30SaveMenuMiscText &t = m.misc_texts[q];
        textout_styled_aligned_ex(dest, get_zc_font((int)t.font), t.text.c_str(), t.x, t.y, t.shadow_type, t.text_align, t.color,
                                  t.shadow_color, -1);
    }

    int y = m.opt_y, txbox_w = 0;

    for(size_t q = 0; q < m.options.size(); ++q)
    {
        const int w = text_length(get_zc_font((int)m.options[q].font), m.options[q].text.c_str());
        if(w > txbox_w)
            txbox_w = w;
    }

    int txbox_x = m.opt_x;

    if(m.textbox_align == 1)
        txbox_x -= txbox_w / 2;
    else if(m.textbox_align == 2)
        txbox_x -= txbox_w;

    for(size_t ind = 0; ind < m.options.size(); ++ind)
    {
        const Q30SaveMenuOption &opt = m.options[ind];
        FONT *f = get_zc_font((int)opt.font);
        const int h = text_height(f), w = text_length(f, opt.text.c_str());
        int tx = txbox_x, ty = y;

        if(m.text_align == 1)
            tx = (tx + txbox_w / 2) - (w / 2);
        else if(m.text_align == 2)
            tx = (tx + txbox_w) - w;

        int color = opt.color, shadow_color = opt.shadow_color;

        if(cursor >= 0 && (int)ind == cursor)
        {
            // the picked colours, except during the blinks while it closes
            if(!clk || !m.close_flash_rate || (clk % (m.close_flash_rate * 2)) < m.close_flash_rate)
            {
                color = opt.picked_color;
                shadow_color = opt.picked_shadow_color;
            }

            tx += m.opt_sel_x_offset;
            ty += m.opt_sel_y_offset;
            overtile16(dest, (int)m.cursor_tile, tx - 16 - m.hspace, ty + (h / 2) - 8, m.cursor_cset, 0);
        }

        textout_styled_aligned_ex(dest, f, opt.text.c_str(), tx, ty, opt.shadow_type, 0, color, shadow_color, -1);
        y += m.vspace + h;
    }

    set_clip_rect(dest, cl, ct, cr - 1, cb - 1);
}

// One frame of input; true when the menu is done (cursor -1: cancelled).
static bool z30_savemenu_tick(const Q30SaveMenu &m, int &cursor, int &clk)
{
    if(m.options.empty())
        return true;

    if(clk)     // no control during the closing animation
        return !--clk;

    const int n = (int)m.options.size();
    int c = cursor >= 0 ? cursor : 0;

    if(rUp())
    {
        c = c ? c - 1 : n - 1;
        if(m.cursor_sfx)
            sfx(m.cursor_sfx);
    }

    if(rDown())
    {
        c = c < n - 1 ? c + 1 : 0;
        if(m.cursor_sfx)
            sfx(m.cursor_sfx);
    }

    cursor = c;
    bool confirm = rSbtn(), cancel = false;

    if(m.flags & Q30_SMENU_CONFIRM_A)
        confirm = rAbtn() || confirm;
    if(m.flags & Q30_SMENU_CANCEL_B)
        cancel = rBbtn();

    if(confirm)
    {
        clk = m.close_frames;
        if(m.choose_sfx)
            sfx(m.choose_sfx);
    }
    else if(cancel)
    {
        cursor = -1;
        return true;
    }

    return false;
}

void playLevelMusic();
void map_bkgsfx(bool on);

// SaveMenu::run: the picked option (-1 none). Quit says what happens next: 2.53's game has no
// reload, so RELOAD ends the game as Game->Reload() does here.
static int z30_savemenu_run(const Q30SaveMenu &m, int cursor = -1, bool continue_point = false)
{
    int clk = 0;
    const bool is_dead = Quit == qGAMEOVER;
    Quit = 0;
    const bool use_music = !(m.flags & Q30_SMENU_DONT_KILL_MUSIC), stop_sounds = !(m.flags & Q30_SMENU_DONT_KILL_SFX);

    if(stop_sounds)
    {
        map_bkgsfx(false);
        kill_sfx();
    }

    load_control_state();
    rSbtn();    // the press that opened it (upstream eats it: zc_readrawkey)

    const bool was_playing = Playing;
    Playing = false;
    bool running = true;

    while(running)
    {
        if(use_music && m.music > 0 && q30_music_exists(m.music))
            q30_play_music(m.music);

        while(!z30_savemenu_tick(m, cursor, clk))
        {
            if(Quit)
            {
                cursor = -1;
                running = false;
                break;
            }

            z30_savemenu_draw(m, framebuf, cursor, clk);
            advanceframe(false);
            load_control_state();
        }

        if(cursor >= 0)
        {
            const Q30SaveMenuOption &opt = m.options[cursor];
            running = false;

            if(opt.script)
                q30_msg_run_generic_frozen(opt.script, opt.initd, 8);

            if(Quit)
                ;
            else if(opt.flags & (Q30_SMENU_OPT_RELOAD | Q30_SMENU_OPT_EXIT))
                Quit = qQUIT;
            else if(opt.flags & Q30_SMENU_OPT_CONTINUE)
                Quit = qCONT;
            else if(!is_dead && (opt.flags & Q30_SMENU_OPT_CANCEL))
                Quit = 0;
            else
                running = true;

            if(opt.flags & Q30_SMENU_OPT_SAVE)
            {
                if(continue_point)
                {
                    game->set_continue_scrn(currscr >= 0x80 ? homescr : currscr);
                    game->set_continue_dmap(currdmap);
                    lastentrance_dmap = currdmap;
                    lastentrance = game->get_continue_scrn();
                }

                save_game(false);
            }
        }
        else if(Quit)
            running = false;
        else if(!is_dead)       // cancelled with B
        {
            Quit = 0;
            running = false;
        }
    }

    if(Quit)
        skipcont = 1;
    else
    {
        if(use_music)
            playLevelMusic();
        if(stop_sounds)
            map_bkgsfx(true);
    }

    Playing = was_playing;
    return cursor;
}

void q30_savemenu_game_over()
{
    if(zasm30_ffscript_version == 0 && quest_header_zelda_version < 0x255)
        return;

    const int ix = zasm30_savemenu_game_over - 1;

    if(ix >= 0 && ix < Q30_NUM_SAVE_MENUS && z30_savemenu_valid(zasm30_save_menus[ix]))
        z30_savemenu_run(zasm30_save_menus[ix]);
}

// F6 and Game->ShowF6Screen in a 2.55/3.0 quest (upstream's onTryQuit, then runF6Engine at the
// end of a frame): the quest's F6 global script runs as a menu, else its F6 save menu opens,
// else the end-game question. With qr_OLD_F6, the end-game question at once, as before.
enum { Z30_QR_OLD_F6 = 175 };
static bool zasm30_f6_pending = false, zasm30_f6_active = false;

bool q30_try_quit()
{
    if(!zasm30_ffscript_version && quest_header_zelda_version < 0x255)
        return false;   // 2.53: its own F6

    if(q30_quest_rule(Z30_QR_OLD_F6))
        f_Quit(qQUIT);
    else
        zasm30_f6_pending = true;

    return true;
}

void q30_run_f6_engine()
{
    // (not inside a running script, nor inside the F6 menu itself: its frames come back here)
    if(!zasm30_f6_pending || zasm30_f6_active || Quit || zasm30_run_depth > 0)
        return;

    zasm30_f6_active = true;
    const int ix = zasm30_savemenu_f6 - 1;

    if(q30_global_scripts[6] && q30_global_scripts[6][0].command != 0xFFFF)
    {
        const int opening = black_opening_count;
        black_opening_count = 0;    // no opening wipe during the F6 menu
        pause_all_sfx();
        zasm30_drop_instance(SCRIPT_GLOBAL, 6);
        zasm30_menu_script(SCRIPT_GLOBAL, 6, 6);
        resume_all_sfx();
        black_opening_count = opening;

        if(!Quit && !get_bit(quest_rules, qr_NOCONTINUE))
            f_Quit(qQUIT);
    }
    else if(ix >= 0 && ix < Q30_NUM_SAVE_MENUS && z30_savemenu_valid(zasm30_save_menus[ix]))
        z30_savemenu_run(zasm30_save_menus[ix]);
    else
        f_Quit(qQUIT);

    zasm30_f6_pending = false;
    zasm30_f6_active = false;
}

static void z30_show_f6()
{
    q30_try_quit();
}

static bool z30_savemenu_command(word cmd)
{
    switch(cmd)
    {
    case Z30C_LOADSAVEMENU:
    {
        const long val = get_register(sarg1) / 10000;
        if(unsigned(val - 1) >= (unsigned)Q30_NUM_SAVE_MENUS)
            Z_scripterrlog("Tried to load invalid save_menu index '%ld'\n", val);
        Z30_REF(SAVEMENU) = val;
        ri->d[2] = val;
        return true;
    }

    case Z30C_SAVEMENU_GETNAME:
    case Z30C_SAVEMENU_SETNAME:
        if(Q30SaveMenu *m = z30_savemenu(Z30_REF(SAVEMENU)))
        {
            const long ptr = get_register(sarg1) / 10000;
            if(cmd == Z30C_SAVEMENU_GETNAME)
                ArrayH::setArray(ptr, m->name);
            else
                ArrayH::getString(ptr, m->name, Q30_SAVEMENU_NAME_LENGTH);
        }
        return true;

    case Z30C_SAVEMENU_REMOVE_OPT:
    case Z30C_SAVEMENU_REMOVE_MISCTEXT:
        if(Q30SaveMenu *m = z30_savemenu(Z30_REF(SAVEMENU)))
        {
            const long ix = get_register(sarg1) / 10000;
            if(cmd == Z30C_SAVEMENU_REMOVE_OPT && unsigned(ix) < m->options.size())
                m->options.erase(m->options.begin() + ix);
            else if(cmd == Z30C_SAVEMENU_REMOVE_MISCTEXT && unsigned(ix) < m->misc_texts.size())
                m->misc_texts.erase(m->misc_texts.begin() + ix);
        }
        return true;

    case Z30C_SAVEMENU_ADD_OPT:
        if(Q30SaveMenu *m = z30_savemenu(Z30_REF(SAVEMENU)))
        {
            if(m->options.size() >= (size_t)Q30_MAX_SAVEMENU_OPTIONS)
            {
                Z_scripterrlog("Save menu %ld already has %d options, no more can be added!\n", Z30_REF(SAVEMENU), (int)m->options.size());
                return true;
            }

            // (upstream inserts with options.insert(it, {}), which is the empty initializer
            // list's insert: an index inside the list adds nothing; only past the end adds one)
            const long ix = get_register(sarg1) / 10000;
            if(unsigned(ix) >= m->options.size())
                m->options.push_back(Q30SaveMenuOption());
        }
        return true;

    case Z30C_SAVEMENU_ADD_MISCTEXT:
        if(Q30SaveMenu *m = z30_savemenu(Z30_REF(SAVEMENU)))
        {
            if(m->misc_texts.size() >= (size_t)Q30_MAX_SAVEMENU_MISC_TEXTS)
            {
                Z_scripterrlog("Save menu %ld already has %d misc texts, no more can be added!\n", Z30_REF(SAVEMENU), (int)m->misc_texts.size());
                return true;
            }

            const long ix = get_register(sarg1) / 10000;    // (as AddOption: inside the list, nothing)
            if(unsigned(ix) >= m->misc_texts.size())
                m->misc_texts.push_back(Q30SaveMenuMiscText());
        }
        return true;

    case Z30C_SAVEMENU_SWAP_OPT:
    case Z30C_SAVEMENU_SWAP_MISCTEXT:
        if(Q30SaveMenu *m = z30_savemenu(Z30_REF(SAVEMENU)))
        {
            const long a = get_register(sarg1) / 10000, b = get_register(sarg2) / 10000;
            const size_t n = cmd == Z30C_SAVEMENU_SWAP_OPT ? m->options.size() : m->misc_texts.size();

            if(unsigned(a) >= n || unsigned(b) >= n || a == b)
                return true;

            if(cmd == Z30C_SAVEMENU_SWAP_OPT)
                std::swap(m->options[a], m->options[b]);
            else
                std::swap(m->misc_texts[a], m->misc_texts[b]);
        }
        return true;

    case Z30C_SAVEMENU_OPT_GET_STRING:
    case Z30C_SAVEMENU_OPT_SET_STRING:
    case Z30C_SAVEMENU_MISCTEXT_GET_STRING:
    case Z30C_SAVEMENU_MISCTEXT_SET_STRING:
        if(Q30SaveMenu *m = z30_savemenu(Z30_REF(SAVEMENU)))
        {
            const long ix = get_register(sarg1) / 10000, ptr = get_register(sarg2) / 10000;
            const bool opt = cmd == Z30C_SAVEMENU_OPT_GET_STRING || cmd == Z30C_SAVEMENU_OPT_SET_STRING;
            const bool get = cmd == Z30C_SAVEMENU_OPT_GET_STRING || cmd == Z30C_SAVEMENU_MISCTEXT_GET_STRING;

            if(unsigned(ix) >= (opt ? m->options.size() : m->misc_texts.size()))
                return true;

            std::string &text = opt ? m->options[ix].text : m->misc_texts[ix].text;

            if(get)
                ArrayH::setArray(ptr, text);
            else
                ArrayH::getString(ptr, text, opt ? Q30_SAVEMENU_OPTION_TEXT_LENGTH : Q30_SAVEMENU_MISC_TEXT_LENGTH);
        }
        return true;

    case Z30C_SAVEMENU_OPEN:
        if(Q30SaveMenu *m = z30_savemenu(Z30_REF(SAVEMENU)))
        {
            const int cursor = z30_savemenu_run(*m);
            ri->d[2] = cursor * 10000;
            // (upstream stops the script when the menu quits the game: run_script ends it as Quit is set)
        }
        return true;
    }

    return false;
}

// ==== portals (upstream's portal sprites and savedportals) =====================================

class Z30Portal : public sprite
{
public:
    int destdmap, destscr, weffect, wsfx, aclk, aframe, aspd, frames, o_tile;
    long saved_data;
    bool prox_active;

    Z30Portal() : sprite(), destdmap(-1), destscr(0), weffect(0), wsfx(0), aclk(0), aframe(0), aspd(0), frames(0), o_tile(0), saved_data(0),
        prox_active(false)
    {
        id = 0;     // (a negative id isn't drawn)
    }

    void loadgfx(int spr)
    {
        const wpndata &w = wpnsbuf[(spr >= 0 && spr < MAXWPNS) ? spr : 0];
        o_tile = w.tile;
        aspd = w.speed ? w.speed : 1;
        frames = w.frames ? w.frames : 1;
        aframe = aclk = 0;
        cs = w.csets & 0xF;
        tile = o_tile;
    }

    virtual bool animate(int)
    {
        if(++aclk >= aspd)
        {
            aclk = 0;

            if(++aframe >= frames)
                aframe = 0;
        }

        tile = o_tile + aframe;
        return false;
    }
};

struct Z30SavedPortal
{
    long uid;
    int destdmap, srcdmap, srcscr, destscr;
    long x, y;
    int sfx, warpfx, spr;
};

enum { Z30_MAX_SAVED_PORTALS = 10000, Z30_MIRROR_SAVED_UID = 1 };
static sprite_list zasm30_portals;
static std::vector<Z30SavedPortal> zasm30_user_portals;
static long zasm30_next_saved_portal = Z30_MIRROR_SAVED_UID + 1;

static Z30Portal *z30_portal(long ref, bool quiet = false)
{
    Z30Portal *p = (Z30Portal *)zasm30_portals.getByUID(ref);

    if(!p && !quiet && ref != -1)
        Z_scripterrlog("Invalid portal pointer: %ld\n", ref);

    return p;
}

static Z30SavedPortal *z30_saved_portal(long uid, bool quiet = false)
{
    for(size_t i = 0; uid && i < zasm30_user_portals.size(); ++i)
        if(zasm30_user_portals[i].uid == uid)
            return &zasm30_user_portals[i];

    if(!quiet && uid != Z30_MIRROR_SAVED_UID)
        Z_scripterrlog("Invalid savedportal pointer: %ld\n", uid);

    return NULL;
}

// upstream's loadportal: a portal standing on the saved one's screen, if Link is there
static Z30Portal *z30_load_portal(const Z30SavedPortal &s)
{
    if(currdmap != s.srcdmap || s.srcscr != currscr || s.destdmap < 0 || s.destdmap >= MAXDMAPS)
        return NULL;

    Z30Portal *p = new Z30Portal();
    p->destdmap = s.destdmap;
    p->destscr = s.destscr + DMaps[s.destdmap].xoff;
    p->weffect = s.warpfx;
    p->wsfx = s.sfx;
    p->loadgfx(s.spr);
    p->x = fix(s.x / 10000.0);
    p->y = fix(s.y / 10000.0);
    p->saved_data = s.uid;
    return p;
}

void q30_portals_clear()        // ALLOFF: as upstream's portals.clear()
{
    zasm30_portals.clear();
}

void q30_portals_reset()        // a new game
{
    zasm30_portals.clear();
    zasm30_user_portals.clear();
    zasm30_next_saved_portal = Z30_MIRROR_SAVED_UID + 1;
}

void q30_draw_script_portals(BITMAP *dest)
{
    if(zasm30_portals.Count())
        zasm30_portals.draw(dest, true);
}

static void z30_portals_prox()
{
    for(int i = 0; i < zasm30_portals.Count(); ++i)
    {
        Z30Portal *p = (Z30Portal *)zasm30_portals.spr(i);
        p->prox_active = !(abs(int(Link.getX()) - int(p->x)) < 12 && abs(int(Link.getY()) - int(p->y)) < 12);
    }
}

// Each frame, with Link's (upstream's handle_portal_collide for every portal)
void q30_portals_frame()
{
    for(int i = 0; i < zasm30_portals.Count(); ++i)
    {
        Z30Portal *p = (Z30Portal *)zasm30_portals.spr(i);
        p->animate(0);

        if(p->destdmap < 0 || p->destdmap >= MAXDMAPS)
            continue;

        if(!(abs(int(Link.getX()) - int(p->x)) < 12 && abs(int(Link.getY()) - int(p->y)) < 12))
        {
            p->prox_active = true;
            continue;
        }

        if(!p->prox_active)
            continue;

        const long saved = p->saved_data;
        const int x = int(p->x) % 256, y = int(p->y) % 176;

        if(q30_portal_warp(x, y, p->destdmap, p->destscr, p->weffect, p->wsfx))
            z30_portals_prox();
        else if(saved)
        {
            // used up (game->clear_portal)
            for(size_t k = 0; k < zasm30_user_portals.size(); ++k)
                if(zasm30_user_portals[k].uid == saved)
                {
                    zasm30_user_portals.erase(zasm30_user_portals.begin() + k);
                    break;
                }
        }

        return;     // (the list may have changed with the warp)
    }
}

static bool z30_portal_command(word cmd)
{
    switch(cmd)
    {
    case Z30C_LOADPORTAL:
    {
        long val = get_register(sarg1) / 10000;

        if(val != -1)
        {
            if(sprite *p = zasm30_portals.spr(val))
                val = p->getUID();
            else
            {
                Z_scripterrlog("Tried to load invalid portal index '%ld'\n", val);
                val = 0;
            }
        }

        Z30_REF(PORTAL) = val;
        ri->d[2] = val;
        return true;
    }

    case Z30C_CREATEPORTAL:
    {
        Z30Portal *p = new Z30Portal();
        long uid = 0;

        if(zasm30_portals.add(p))
            uid = p->getUID();
        else
            Z_scripterrlog("Unable to create new portal! Limit reached!\n");

        Z30_REF(PORTAL) = uid;
        ri->d[2] = uid;
        return true;
    }

    case Z30C_LOADSAVPORTAL:
    {
        const long val = get_register(sarg1) / 10000;
        const bool ok = val == Z30_MIRROR_SAVED_UID || z30_saved_portal(val) != NULL;     // (the mirror's is always there)
        Z30_REF(SAVPORTAL) = ok ? val : 0;
        ri->d[2] = Z30_REF(SAVPORTAL);
        return true;
    }

    case Z30C_CREATESAVPORTAL:
    {
        if(zasm30_user_portals.size() >= (size_t)Z30_MAX_SAVED_PORTALS)
        {
            Z30_REF(SAVPORTAL) = ri->d[2] = 0;
            Z_scripterrlog("Cannot create any more Saved Portals! Remove some first!\n");
            return true;
        }

        Z30SavedPortal s = { zasm30_next_saved_portal++, -1, -1, 0, 0, 0, 0, 0, 0, 0 };
        zasm30_user_portals.push_back(s);
        Z30_REF(SAVPORTAL) = ri->d[2] = s.uid;
        return true;
    }

    case Z30C_PORTALREMOVE:
    {
        const long ref = Z30_REF(PORTAL);

        if(ref == -1)
            q30_mirror_portal_remove(false);
        else if(Z30Portal *p = z30_portal(ref, true))
        {
            for(int i = 0; i < zasm30_portals.Count(); ++i)
                if(zasm30_portals.spr(i) == p)
                {
                    zasm30_portals.del(i);
                    break;
                }
        }
        return true;
    }

    case Z30C_PORTALUSESPRITE:
    {
        const long spr = get_register(sarg1) / 10000;

        if(BC::checkWeaponMiscSprite(spr, "portal->UseSprite()") != SH::_NoError)
            return true;

        if(Z30Portal *p = z30_portal(Z30_REF(PORTAL)))
            p->loadgfx((int)spr);
        return true;
    }

    case Z30C_SAVEDPORTALREMOVE:
    {
        const long uid = Z30_REF(SAVPORTAL);

        if(uid == Z30_MIRROR_SAVED_UID)
            q30_mirror_portal_remove(true);
        else if(z30_saved_portal(uid, true))
        {
            for(int i = 0; i < zasm30_portals.Count(); ++i)
                if(((Z30Portal *)zasm30_portals.spr(i))->saved_data == uid)
                    ((Z30Portal *)zasm30_portals.spr(i))->saved_data = 0;

            for(size_t k = 0; k < zasm30_user_portals.size(); ++k)
                if(zasm30_user_portals[k].uid == uid)
                {
                    zasm30_user_portals.erase(zasm30_user_portals.begin() + k);
                    break;
                }
        }
        return true;
    }

    case Z30C_SAVEDPORTALGENERATE:
    {
        const long uid = Z30_REF(SAVPORTAL);
        long ret = 0;

        if(uid == Z30_MIRROR_SAVED_UID)
            ret = -1;       // (getPortalFromSaved: the mirror's own portal)
        else if(Z30SavedPortal *s = z30_saved_portal(uid))
        {
            for(int i = 0; i < zasm30_portals.Count() && !ret; ++i)
                if(((Z30Portal *)zasm30_portals.spr(i))->saved_data == uid)
                    ret = zasm30_portals.spr(i)->getUID();

            if(!ret)
            {
                if(Z30Portal *p = z30_load_portal(*s))
                {
                    if(zasm30_portals.add(p))
                        ret = p->getUID();
                }
            }
        }

        ri->d[2] = ret;
        return true;
    }
    }

    return false;
}

// ==== websockets: as upstream's player behaves with no network to reach ========================
// (its native pool: the connection stays Connecting, with no error and no messages; nothing is sent)

enum { Z30_MAX_WEBSOCKETS = 3, Z30_WS_BASE = 0x7E3FFF00 };

struct Z30WebSocket
{
    bool used;
    std::string url, err;
};

static Z30WebSocket zasm30_websockets[Z30_MAX_WEBSOCKETS];

static Z30WebSocket *z30_websocket(long ref, bool quiet = false)
{
    const long n = ref - Z30_WS_BASE - 1;

    if(n >= 0 && n < Z30_MAX_WEBSOCKETS && zasm30_websockets[n].used)
        return &zasm30_websockets[n];

    if(!quiet)
        Z_scripterrlog("Invalid websocket using UID = %ld\n", ref);
    return NULL;
}

static bool z30_websocket_command(word cmd)
{
    switch(cmd)
    {
    case Z30C_WEBSOCKET_OWN:
        z30_websocket(Z30_REF(WEBSOCKET));
        return true;

    case Z30C_WEBSOCKET_LOAD:
    {
        std::string url;
        ArrayH::getString(get_register(sarg1) / 10000, url, 512);
        Z30_REF(WEBSOCKET) = 0;
        ri->d[2] = 0;

        if(url.empty() || url.compare(0, 2, "ws") != 0)
            return true;

        for(int n = 0; n < Z30_MAX_WEBSOCKETS; ++n)
            if(!zasm30_websockets[n].used)
            {
                zasm30_websockets[n].used = true;
                zasm30_websockets[n].url = url;
                zasm30_websockets[n].err.clear();
                Z30_REF(WEBSOCKET) = ri->d[2] = Z30_WS_BASE + n + 1;
                return true;
            }

        Z_scripterrlog("Scripts have used all %d websockets.\n", (int)Z30_MAX_WEBSOCKETS);
        return true;
    }

    case Z30C_WEBSOCKET_FREE:
        if(Z30WebSocket *ws = z30_websocket(Z30_REF(WEBSOCKET), true))
        {
            ws->used = false;
            ws->url.clear();
        }
        return true;

    case Z30C_WEBSOCKET_ERROR:
    {
        const long ptr = get_register(sarg1) / 10000;
        Z30WebSocket *ws = z30_websocket(Z30_REF(WEBSOCKET));
        ArrayH::setArray(ptr, std::string(ws ? ws->err : "Invalid pointer"));
        return true;
    }

    case Z30C_WEBSOCKET_SEND:
    {
        const long type = get_register(sarg1);

        if(BC::checkBounds(type, 1, 2, "WebsocketType") != SH::_NoError)
            return true;

        z30_websocket(Z30_REF(WEBSOCKET));      // nothing goes out
        return true;
    }

    case Z30C_WEBSOCKET_RECEIVE:
        if(z30_websocket(Z30_REF(WEBSOCKET)))
            set_register(sarg1, 0);             // never a message
        return true;
    }

    return false;
}

// ==== subscreen objects (upstream's SUBDATA_/SUBPAGE_/SUBWIDG_ commands) =====================

// A widget type's 2.53 object (as q255_init.cpp converts them), ssoNULL for the ones it doesn't draw.
static int z30_widget_sso(int ty)
{
    switch(ty)
    {
    case Z30W_FRAME: return sso2X2FRAME;
    case Z30W_TEXT: return ssoTEXT;
    case Z30W_LINE: return ssoLINE;
    case Z30W_RECT: return ssoRECT;
    case Z30W_TIME: return ssoSSTIME;
    case Z30W_MMETER: return ssoMAGICMETER;
    case Z30W_LMETER: return ssoLIFEMETER;
    case Z30W_BTNITM: return ssoBUTTONITEM;
    case Z30W_COUNTER: return ssoCOUNTER;
    case Z30W_OLDCTR: return ssoCOUNTERS;
    case Z30W_MMAPTITLE: return ssoMINIMAPTITLE;
    case Z30W_MMAP: return ssoMINIMAP;
    case Z30W_LMAP: return ssoLARGEMAP;
    case Z30W_BGCOLOR: return ssoCLEAR;
    case Z30W_ITEMSLOT: return ssoCURRENTITEM;
    case Z30W_MCGUFF_FRAME: return ssoTRIFRAME;
    case Z30W_MCGUFF: return ssoTRIFORCE;
    case Z30W_TILEBLOCK: return ssoTILEBLOCK;
    case Z30W_MINITILE: return ssoMINITILE;
    case Z30W_SELECTOR: return ssoSELECTOR1;
    case Z30W_LGAUGE: return ssoLIFEGAUGE;
    case Z30W_MGAUGE: return ssoMAGICGAUGE;
    case Z30W_TEXTBOX: return ssoTEXTBOX;
    case Z30W_SELECTEDTEXT: return ssoSELECTEDITEMNAME;
    }

    return ssoNULL;
}

// The objects of page pg of s: the live ones when it is the page shown, else its kept copy
// (-1: it has none kept).
static int z30_page_object_count(Z30Subscreen &s, int pg, subscreen_object **live)
{
    *live = NULL;

    if(s.slot >= 0 && s.slot < MAXCUSTOMSUBSCREENS && pg == s.curpage)
    {
        *live = custom_subscreen[s.slot].objects;
        int n = 0;

        while(n < MAXSUBSCREENITEMS && (*live)[n].type != ssoNULL)
            ++n;

        return n;
    }

    return s.pages[pg].stored ? (int)s.pages[pg].objs.size() : -1;
}

static void z30_keep_shown_page(Z30Subscreen &s)
{
    if(s.slot >= 0 && s.slot < MAXCUSTOMSUBSCREENS && s.curpage >= 0 && s.curpage < (int)s.pages.size())
        z30_store_objects(s.pages[s.curpage], custom_subscreen[s.slot].objects, MAXSUBSCREENITEMS);
}

static void z30_show_page(Z30Subscreen &s)
{
    if(s.slot >= 0 && s.slot < MAXCUSTOMSUBSCREENS && s.curpage >= 0 && s.curpage < (int)s.pages.size())
        z30_load_objects(s.pages[s.curpage], custom_subscreen[s.slot].objects);
}

static int zasm30_queued_sub_ty = -1, zasm30_queued_sub = -1;

// A script's subscreendata->Open() (upstream's queued_subscreen): opened where Link would open
// his, then the DMap's own again. False when none is queued.
bool q30_open_queued_subscreen()
{
    const int ty = zasm30_queued_sub_ty, sub = zasm30_queued_sub;

    if(ty < 0)
        return false;

    zasm30_queued_sub_ty = zasm30_queued_sub = -1;  // (before dosubscr: a script can queue another)

    if(sub < 0 || sub >= (int)zasm30_subs[ty].size())
        return false;

    const int slot = zasm30_subs[ty][sub].slot;

    if(slot >= 0 && slot < MAXCUSTOMSUBSCREENS)
    {
        subscreen_group *was = current_subscreen_active;
        current_subscreen_active = &custom_subscreen[slot];
        dosubscr(&QMisc);
        current_subscreen_active = was;
    }

    return true;
}

static void z30_page_swap_widgets(Z30Subscreen &s, int pg, int a, int b)
{
    Z30SubPage &p = s.pages[pg];
    std::swap(p.widgets[a], p.widgets[b]);

    // the objects change places too, so the drawing order is the widgets' (as upstream's)
    const int oa = p.widgets[a].obj, ob = p.widgets[b].obj;
    subscreen_object *live;
    const int n = z30_page_object_count(s, pg, &live);

    if(oa < 0 || ob < 0 || oa >= n || ob >= n)
        return;

    if(live)
        std::swap(live[oa], live[ob]);
    else
    {
        std::swap(p.objs[oa], p.objs[ob]);
        std::swap(p.texts[oa], p.texts[ob]);
    }

    const int ca = oa < (int)p.obj_cond.size() ? p.obj_cond[oa] : -1, cb = ob < (int)p.obj_cond.size() ? p.obj_cond[ob] : -1;

    if(ca >= 0 || cb >= 0)
    {
        if((int)p.obj_cond.size() <= zc_max(oa, ob))
            p.obj_cond.resize(zc_max(oa, ob) + 1, -1);
        p.obj_cond[oa] = cb;
        p.obj_cond[ob] = ca;
    }

    p.widgets[a].obj = ob;
    p.widgets[b].obj = oa;
}

static bool z30_sub_sys_command(word cmd)
{
    switch(cmd)
    {
    case Z30C_SUBDATA_OPEN:
    case Z30C_SUBDATA_CLOSE:
    {
        int sub, ty, pg, ind;
        const long ref = Z30_REF(SUBSCREENDATA);

        if(!z30_subdata(ref) || !z30_from_subref(ref, sub, ty, pg, ind))
        {
            Z_scripterrlog("Invalid subscreendata using UID = %ld\n", ref);
            return true;
        }

        if(ty != 0 && ty != 3)
        {
            Z_scripterrlog("This function is only valid for Active and Map subscreens!\n");
            return true;
        }

        if(cmd == Z30C_SUBDATA_OPEN)
        {
            zasm30_queued_sub_ty = ty;
            zasm30_queued_sub = sub;
        }
        else    // only a subscreen's own engine script can close it, and none runs here
            Z_scripterrlog("This function can only be used on the currently-open Active or Map subscreen.\n");
        return true;
    }

    case Z30C_SUBDATA_SWAP_PAGES:
    {
        Z30_REF(SUBSCREENDATA) = SH::read_stack(SPR + 2);
        Z30Subscreen *s = z30_subdata(Z30_REF(SUBSCREENDATA));

        if(!s)
            return true;

        const int p1 = SH::read_stack(SPR + 1) / 10000, p2 = SH::read_stack(SPR) / 10000, n = (int)s->pages.size();

        if(unsigned(p1) >= unsigned(n))
            Z_scripterrlog("Invalid page index '%d' passed to subscreendata->SwapPages()\n", p1);
        else if(unsigned(p2) >= unsigned(n))
            Z_scripterrlog("Invalid page index '%d' passed to subscreendata->SwapPages()\n", p2);
        else
        {
            z30_keep_shown_page(*s);
            std::swap(s->pages[p1], s->pages[p2]);

            if(s->curpage == p1)
                s->curpage = p2;
            else if(s->curpage == p2)
                s->curpage = p1;

            for(int q = 0; q < 4; ++q)
            {
                if((s->def_btns[q] & 0xFF) == p1)
                    s->def_btns[q] = (s->def_btns[q] & 0xFF00) | p2;
                else if((s->def_btns[q] & 0xFF) == p2)
                    s->def_btns[q] = (s->def_btns[q] & 0xFF00) | p1;
            }
        }
        return true;
    }

    case Z30C_SUBPAGE_SWAP_WIDG:
    {
        const long pref = SH::read_stack(SPR + 2);
        Z30_REF(SUBSCREENPAGE) = pref;
        Z30Subscreen *s = z30_subdata(pref);
        Z30SubPage *p = z30_subpage(pref);
        int sub, ty, pg, ind;

        if(!p || !z30_from_subref(pref, sub, ty, pg, ind))
            return true;

        const int w1 = SH::read_stack(SPR + 1) / 10000, w2 = SH::read_stack(SPR) / 10000, n = (int)p->widgets.size();

        if(unsigned(w1) >= unsigned(n))
            Z_scripterrlog("Invalid first widget index '%d' passed to subscreenpage->SwapWidgets()\n", w1);
        else if(unsigned(w2) >= unsigned(n))
            Z_scripterrlog("Invalid second widget index '%d' passed to subscreenpage->SwapWidgets()\n", w2);
        else if(w1 != w2)
            z30_page_swap_widgets(*s, pg, w1, w2);
        return true;
    }

    case Z30C_SUBPAGE_MOVE_SEL:
    {
        ri->d[2] = 0;
        const long pref = SH::read_stack(SPR + 3);
        Z30_REF(SUBSCREENPAGE) = pref;
        Z30Subscreen *s = z30_subdata(pref);
        int sub, ty, pg, ind;

        if(!s || !z30_subpage(pref) || !z30_from_subref(pref, sub, ty, pg, ind))
            return true;

        const int flags = SH::read_stack(SPR) / 10000, dir = SH::read_stack(SPR + 1) / 10000, pos = SH::read_stack(SPR + 2) / 10000;
        const int sel = dir == up ? SEL_UP : dir == down ? SEL_DOWN : dir == left ? SEL_LEFT : SEL_RIGHT;
        const int np = z30_btn_movepos(*s, pg, sel, ((pos & 0xFF) << 8) | (pg & 0xFF), 255, 255, 255, (flags & 0x01) != 0,
                                       (flags & 0x02) != 0) >> 8;
        ri->d[2] = np * 10000;
        return true;
    }

    case Z30C_SUBPAGE_NEW_WIDG:
    {
        ri->d[2] = 0;
        const long pref = SH::read_stack(SPR + 1);
        Z30_REF(SUBSCREENPAGE) = pref;
        Z30Subscreen *s = z30_subdata(pref);
        Z30SubPage *p = z30_subpage(pref);
        int sub, ty, pg, ind;

        if(!p || !z30_from_subref(pref, sub, ty, pg, ind) || p->widgets.size() >= 0x2000)
            return true;

        const int wty = SH::read_stack(SPR) / 10000;

        if(wty <= Z30W_NULL || wty >= Z30W_MAX)
        {
            Z_scripterrlog("Invalid type %d passed to subscreenpage->CreateWidget()\n", wty);
            return true;
        }

        Z30SubWidget wd = Z30SubWidget();
        wd.type = wty;
        wd.obj = -1;
        wd.posflags = sspUP | sspDOWN | sspSCROLLING;
        wd.w = wd.h = 1;

        // its 2.53 object, last on the page (drawn over the others, as upstream's)
        const int sso = z30_widget_sso(wty);
        subscreen_object *live = NULL;
        const int n = sso == ssoNULL ? -1 : z30_page_object_count(*s, pg, &live);

        if(n >= 0 && n < MAXSUBSCREENITEMS - 1)
        {
            subscreen_object o;
            memset(&o, 0, sizeof(o));
            o.type = sso;
            o.pos = sspUP | sspDOWN | sspSCROLLING;
            o.w = o.h = 1;

            if(live)
            {
                if(z30_sub_has_text(sso))
                {
                    char *c = new char[1];
                    c[0] = 0;
                    o.dp1 = c;
                }
                live[n] = o;
            }
            else
            {
                p->objs.push_back(o);
                p->texts.push_back(std::string());
            }

            wd.obj = n;
        }

        p->widgets.push_back(wd);
        ri->d[2] = z30_subref(sub, ty, pg, (int)p->widgets.size() - 1);
        return true;
    }

    case Z30C_SUBPAGE_DELETE:
    {
        const long pref = Z30_REF(SUBSCREENPAGE);
        Z30Subscreen *s = z30_subdata(pref);
        int sub, ty, pg, ind;

        if(!s || !z30_subpage(pref) || !z30_from_subref(pref, sub, ty, pg, ind))
            return true;

        z30_keep_shown_page(*s);

        if(s->pages.size() == 1)
        {
            Z30SubPage empty = Z30SubPage();
            empty.stored = true;
            empty.cursor_pos = 0;
            s->pages[0] = empty;
        }
        else
        {
            if(s->curpage >= pg)
                --s->curpage;
            if(s->curpage < 0)      // (upstream's byte wraps to 255 here; the first page is kept shown)
                s->curpage = 0;

            for(int q = 0; q < 4; ++q)
            {
                if((s->def_btns[q] & 0xFF) > pg && (s->def_btns[q] & 0xFF) != 0xFF)
                    --s->def_btns[q];
                else if((s->def_btns[q] & 0xFF) == pg)
                    s->def_btns[q] = 255;
            }

            s->pages.erase(s->pages.begin() + pg);
        }

        z30_show_page(*s);
        return true;
    }

    case Z30C_SUBWIDG_GET_SELTEXT_OVERRIDE:
    case Z30C_SUBWIDG_SET_SELTEXT_OVERRIDE:
    {
        Z30SubWidget *w = z30_subwidg(Z30_REF(SUBSCREENWIDG));
        if(!w)
            return true;
        const long ptr = get_register(sarg1) / 10000;
        if(cmd == Z30C_SUBWIDG_GET_SELTEXT_OVERRIDE)
            ArrayH::setArray(ptr, w->seltext);
        else
            ArrayH::getString(ptr, w->seltext, 65535);
        return true;
    }

    case Z30C_SUBWIDG_CHECK_CONDITIONS:
    case Z30C_SUBWIDG_CHECK_VISIBLE:
    {
        int sub, ty, pg, ind;
        const long wref = Z30_REF(SUBSCREENWIDG);
        Z30SubWidget *w = z30_subwidg(wref);

        if(!w || !z30_from_subref(wref, sub, ty, pg, ind))
            return true;

        const Z30SubPage &p = zasm30_subs[ty][sub].pages[pg];
        bool ok = true;

        if(w->obj >= 0 && w->obj < (int)p.obj_cond.size() && p.obj_cond[w->obj] >= 0 && p.obj_cond[w->obj] < (int)p.conds.size())
            ok = z30_cond_ok(p.conds[p.obj_cond[w->obj]]);

        if(cmd == Z30C_SUBWIDG_CHECK_VISIBLE && ok)
        {
            const int pos = sspUP;      // (current_subscr_pos as a script sees it outside the open subscreen)
            if(msg_active && (w->posflags & 0x08))      // sspNOMSGSTR
                ok = false;
            else if(!(w->posflags & pos))
                ok = false;
            else if(w->type == Z30W_TIME && !q30_should_show_time())
                ok = false;
        }

        set_register(sarg1, ok ? 10000 : 0);
        return true;
    }

    // Game->SwapSubscreens(type, a, b): upstream takes the two indexes from the stack as they are
    // (not divided by 10000), so only 0 names a subscreen there.
    case Z30C_SWAPSUBSCREENV:
    {
        const long ty = sarg1 / 10000;

        if(ty < 0 || ty > 3)
        {
            Z_scripterrlog("Invalid Subscreen Type '%ld'\n", ty);
            return true;
        }

        std::vector<Z30Subscreen> &v = zasm30_subs[ty];
        const long p1 = SH::read_stack(SPR + 1), p2 = SH::read_stack(SPR);

        if(unsigned(p1) >= v.size())
            Z_scripterrlog("Invalid susbcr index '%ld'\n", p1);
        else if(unsigned(p2) >= v.size())
            Z_scripterrlog("Invalid susbcr index '%ld'\n", p2);
        else if(p1 != p2)
        {
            // the 2.53 slots stay where they are, with each subscreen's objects going along
            const int s1 = v[p1].slot, s2 = v[p2].slot;

            if(s1 >= 0 && s2 >= 0 && s1 < MAXCUSTOMSUBSCREENS && s2 < MAXCUSTOMSUBSCREENS)
                std::swap(custom_subscreen[s1], custom_subscreen[s2]);

            std::swap(v[p1], v[p2]);
            v[p1].slot = s1;
            v[p2].slot = s2;
        }
        return true;
    }
    }

    return false;
}

// subscreendata->TransArgs[] (upstream's SUBDATATRANSARGS): the page change animating now.
static bool z30_sub_trans_args(bool write, long &ret, long value)
{
    ret = 0;
    Z30Subscreen *s = z30_subdata(Z30_REF(SUBSCREENDATA));

    if(!s || s != z30_shown_active())
    {
        Z_scripterrlog("Only valid for the current active/map subscreen!\n");
        return true;
    }

    const long idx = Z30_INDEX;

    if(!g_pg_anim.on || idx < 0 || idx >= 4)
        return true;

    Q30SubTrans &tr = g_pg_anim.tr;
    const long scale = (tr.type == 1 && idx == 1) ? 1 : 10000;     // SubscrTransition::argScale (slide speed)

    if(write)
        tr.arg[idx] = value / scale;
    else
        ret = tr.arg[idx] * scale;

    return true;
}

// ==== other commands ============================================================================

// zinfo's default_itype_strings (the first 88; the rest come out as -zzNNN, as upstream's
// blank ones do)
static const char *const z30_default_itype_names[] =
{
    "Swords", "Boomerangs", "Arrows", "Candles", "Whistles",
    "Bait", "Letters", "Potions", "Wands", "Rings",
    "Wallets", "Amulets", "Shields", "Bows", "Rafts",
    "Ladders", "Books", "Magic Keys", "Bracelets", "Flippers",
    "Boots", "Hookshots", "Lenses", "Hammers", "Din's Fire",
    "Farore's Wind", "Nayru's Love", "Bombs", "Super Bombs", "Clocks",
    "Keys", "Magic Containers", "Triforce Pieces", "Maps", "Compasses",
    "Boss Keys", "Quivers", "Level Keys", "Canes of Byrna", "Rupees",
    "Arrow Ammo", "Fairies", "Magic", "Hearts", "Heart Containers",
    "Heart Pieces", "Kill All Enemies", "Bomb Ammo", "Bomb Bags", "Roc Items",
    "Hover Boots", "Scroll: Spin Attack", "Scroll: Cross Beams", "Scroll: Quake Hammer","Whisp Rings",
    "Charge Rings", "Scroll: Peril Beam", "Wealth Medals", "Heart Rings", "Magic Rings",
    "Scroll: Hurricane Spin", "Scroll: Super Quake","Stones of Agony", "Stomp Boots", "Whimsical Rings",
    "Peril Rings", "Non-gameplay Items", "Custom Itemclass 01", "Custom Itemclass 02", "Custom Itemclass 03",
    "Custom Itemclass 04", "Custom Itemclass 05", "Custom Itemclass 06", "Custom Itemclass 07", "Custom Itemclass 08",
    "Custom Itemclass 09", "Custom Itemclass 10", "Custom Itemclass 11", "Custom Itemclass 12", "Custom Itemclass 13",
    "Custom Itemclass 14", "Custom Itemclass 15", "Custom Itemclass 16", "Custom Itemclass 17", "Custom Itemclass 18",
    "Custom Itemclass 19", "Custom Itemclass 20", "Bow and Arrow (Subscreen Only)", "Letter or Potion (Subscreen Only)"
};
static std::vector<std::string> zasm30_itype_names;     // the quest's own (its ZInfo), "" where it gave none

void q30_set_itemclass_name(int q, const char *name)
{
    if(q < 0 || q >= 512)
        return;
    if((int)zasm30_itype_names.size() <= q)
        zasm30_itype_names.resize(q + 1);
    zasm30_itype_names[q] = name ? name : "";
}

void q30_clear_itemclass_names() { zasm30_itype_names.clear(); }

// zinfo::getItemClassName
static std::string z30_itemclass_name(int q)
{
    if(q >= 0 && q < (int)zasm30_itype_names.size() && !zasm30_itype_names[q].empty())
        return zasm30_itype_names[q];
    if(q >= 0 && q < (int)(sizeof(z30_default_itype_names) / sizeof(z30_default_itype_names[0])))
        return z30_default_itype_names[q];

    char buf[16];
    sprintf(buf, "-zz%03d", q);
    return buf;
}

// _walkflag_layer for Screen->isSolidLayer (layer 0-6; region pixels in a scrolling region)
static bool z30_walkflag_layer(int x, int y, int layer)
{
    int max_x = q30_region_width(), max_y = q30_region_height();

    if(!get_bit(quest_rules, qr_LTTPWALK))
    {
        max_x -= 7;
        max_y -= 7;
    }

    if(x < 0 || y < 0 || x >= max_x || y >= max_y)
        return false;

    int pos = 0;
    bool cur = false;
    mapscr *m = q30_region_rpos_screen(q30_region_rpos_at(x, y), layer, &pos, &cur);

    if(!m || !(m->valid & mVALID) || pos < 0 || pos >= (int)m->data.size())
        return false;

    const newcombo *c = &combobuf[m->data[pos]];
    const bool dried = iswater_type(c->type) && DRIEDLAKE;
    int b = 1;

    if(x & 8) b <<= 2;
    if(y & 8) b <<= 1;

    return (c->walk & b) && !dried;     // (cnt 1: the one point)
}

// paldata->WriteSpritePalette / WriteSpriteCSet: into the 30 sprite palettes (two pages of 15)
static void z30_paldata_write_sprite(bool one_cset)
{
    Z30PalData *pd = z30_paldata(Z30_REF(PALDATA));

    if(!pd)
        return;

    const long page = get_register(sarg1) / 10000;

    if(page != 0 && page != 1)
    {
        Z_scripterrlog("Invalid page (%ld) passed to paldata->%s(). Valid pages are 0 or 1. Aborting.\n", page,
                       one_cset ? "WriteSpriteCSet" : "WriteSpritePalette");
        return;
    }

    const int off = page ? 15 : 0;
    long only = -1;

    if(one_cset)
    {
        only = get_register(sarg2) / 10000;

        if(unsigned(only) > 14)
        {
            Z_scripterrlog("Invalid CSet (%ld) passed to paldata->WriteSpriteCSet(). Valid CSets are 0-14. Aborting.\n", only);
            return;
        }
    }

    bool changed6 = false, changed14 = false;

    for(int q = 0; q < 15; ++q)
    {
        if(only >= 0 && q != only)
            continue;

        if(z30_pal_write_cset(*pd, q, pSprite(off + q)))
        {
            if(off + q == q30_currspal6)
                changed6 = true;
            if(off + q == q30_currspal14)
                changed14 = true;
        }
    }

    if(changed6)
        loadpalset(6, pSprite(q30_currspal6));
    if(changed14)
        loadpalset(14, pSprite(q30_currspal14));
}

void TraceScriptIDs();

static void z30_trace_text(const std::string &s)
{
    TraceScriptIDs();
    al_trace("%s", s.c_str());

    if(zconsole)
        printf("%s", s.c_str());
}

enum { Z30_QR_OLD_DMAP_INTRO_STRINGS = 561 };
void q30_set_dmap_texts(int dmap, const char *title, long intro_string);
long q30_dmap_intro_string(int dmap);

static bool zasm30_sys_command(word cmd)
{
    if(z30_savemenu_command(cmd) || z30_portal_command(cmd) || z30_websocket_command(cmd) || z30_sub_sys_command(cmd))
        return true;

    switch(cmd)
    {
    // -- name lookups ----------------------------------------------------------------------------
    case Z30C_GETITEMBYNAME:    // (GETNPCBYNAME: q30_scripts_live.h)
    case Z30C_GETDMAPBYNAME:
    {
        string name;
        ArrayH::getString(get_register(sarg1) / 10000, name, cmd == Z30C_GETITEMBYNAME ? 512 : 256);
        long num = -1;

        if(cmd == Z30C_GETITEMBYNAME)
        {
            for(int q = 0; q < q30_items_end() && num < 0; ++q)
                if(name == q30_item_name(q))
                    num = q;
        }
        else
        {
            for(int q = 0; q < MAXDMAPS && num < 0; ++q)
                if(!strcmp(name.c_str(), DMaps[q].name))
                    num = q;
        }

        set_register(sarg1, num * 10000);
        return true;
    }

    // -- sounds ----------------------------------------------------------------------------------
    case Z30C_PAUSESOUNDR:
    case Z30C_PAUSESOUNDV:
    case Z30C_RESUMESOUNDR:
    case Z30C_RESUMESOUNDV:
    {
        const bool v = cmd == Z30C_PAUSESOUNDV || cmd == Z30C_RESUMESOUNDV;
        const bool pause = cmd == Z30C_PAUSESOUNDR || cmd == Z30C_PAUSESOUNDV;
        const long id = SH::get_arg(sarg1, v) / 10000;

        if(BC::checkSFXID(id, pause ? "Audio->PauseSound()" : "Audio->ResumeSound()") != SH::_NoError)
            return true;

        if(pause)
            pause_sfx(id);
        else
            resume_sfx(id);
        return true;
    }
    case Z30C_PAUSESFX:
        pause_sfx(ri->d[0] / 10000);
        return true;
    case Z30C_RESUMESFX:
    case Z30C_CONTINUESFX:      // (upstream: cont_sfx was never ported to its newer sound code; it resumes)
        resume_sfx(ri->d[0] / 10000);
        return true;
    case Z30C_PAUSEMUSIC:       // upstream: "these have never worked"; nothing
    case Z30C_RESUMEMUSIC:
        return true;

    // -- screen wipes ----------------------------------------------------------------------------
    case Z30C_OPENWIPE:
        openscreen();
        return true;
    case Z30C_CLOSEWIPE:
        closescreen();
        return true;
    case Z30C_OPENWIPESHAPE:
    case Z30C_CLOSEWIPESHAPE:
    {
        long shape = get_register(sarg1) / 10000;

        if(shape < 0 || shape >= bosMAX)
        {
            Z_scripterrlog("Invalid shape passed to %s! Valid range %d to %d. Using 'Circle' shape.\n",
                           cmd == Z30C_OPENWIPESHAPE ? "Screen->OpeningWipe(int32_t)" : "Screen->ClosingWipe(int32_t)", 0, bosMAX - 1);
            shape = bosCIRCLE;
        }

        if(cmd == Z30C_OPENWIPESHAPE)
            openscreen((int)shape);
        else
            closescreen((int)shape);
        return true;
    }

    // -- numbers ---------------------------------------------------------------------------------
    case Z30C_SRNDR:
    case Z30C_SRNDV:
        srand((unsigned int)SH::get_arg(sarg1, cmd == Z30C_SRNDV));     // (not divided: the decimals count too)
        return true;
    case Z30C_SRNDRND:
    {
        const int r1 = rand(), r2 = rand(), sign = (rand() % 2) ? 1 : -1;
        const int32_t seed = (int32_t)(int64_t)((int64_t)time(0) + (int64_t)r1 * (int64_t)r2 * sign);
        set_register(sarg1, seed);
        srand((unsigned int)seed);
        return true;
    }
    case Z30C_LPOWERR:
    case Z30C_LPOWERV:
    case Z30C_LPOWERV2:
    {
        // do_lpower(v, inv): raw values (no /10000); V2 swaps which argument is the literal
        const bool inv = cmd == Z30C_LPOWERV2;
        const long ex = inv ? get_register(sarg2) : SH::get_arg(sarg2, cmd == Z30C_LPOWERV);
        const long base = inv ? sarg1 : get_register(sarg1);
        const long dest = inv ? sarg2 : sarg1;

        if(ex == 0 && base == 0)
            set_register(dest, 1);
        else
            set_register(dest, (long)(int32_t)pow((double)base, (double)ex));
        return true;
    }
    case Z30C_COMPAREV2:
    {
        // do_comp(true, true): the literal is the first operand, the register the second
        const long temp = get_register(sarg2), temp2 = sarg1;
        zasm30_cmp_op1 = temp2;
        zasm30_cmp_op2 = temp;

        if(temp2 >= temp) ri->scriptflag |= MOREFLAG;
        else              ri->scriptflag &= ~MOREFLAG;

        if(temp2 == temp) ri->scriptflag |= TRUEFLAG;
        else              ri->scriptflag &= ~TRUEFLAG;
        return true;
    }

    // -- text --------------------------------------------------------------------------------------
    case Z30C_MESSAGEWIDTHR:
    case Z30C_MESSAGEHEIGHTR:
    {
        const long id = get_register(sarg1) / 10000;

        if(BC::checkMessage(id, cmd == Z30C_MESSAGEWIDTHR ? "Text->MessageWidth()" : "Text->MessageHeight()") != SH::_NoError)
        {
            ri->d[2] = -10000;
            return true;
        }

        FONT *f = get_zc_font(MsgStrings[id].font);

        if(cmd == Z30C_MESSAGEHEIGHTR)
            ri->d[2] = text_height(f) * 10000;
        else
        {
            const char *lt = q30_msg_long_text((int)id);
            std::string s(lt ? lt : MsgStrings[id].s);
            const size_t last = s.find_last_not_of(' ');
            s = last == std::string::npos ? std::string() : s.substr(0, last + 1);
            ri->d[2] = text_length(f, s.c_str()) * 10000;
        }
        return true;
    }
    case Z30C_TRACELR:
    {
        char buf[32];
        sprintf(buf, "%ld\n", (long)(int32_t)get_register(sarg1));
        z30_trace_text(buf);
        return true;
    }
    case Z30C_PRINTFA:
    case Z30C_SPRINTFA:
    {
        // printfa(format, args[]) and sprintfa(dest, format, args[])
        const bool to_str = cmd == Z30C_SPRINTFA;
        const long fmt_ptr = SH::read_stack(SPR + 1) / 10000, args_ptr = SH::read_stack(SPR) / 10000;
        const long dest = to_str ? SH::read_stack(SPR + 2) / 10000 : 0;
        ZScriptArray &fa = ArrayH::getArray(fmt_ptr);
        ZScriptArray &aa = ArrayH::getArray(args_ptr);

        if(to_str)
            ri->d[2] = 0;

        if(&fa == &INVALIDARRAY || &aa == &INVALIDARRAY || (to_str && &ArrayH::getArray(dest) == &INVALIDARRAY))
            return true;

        std::string format;
        ArrayH::getString(fmt_ptr, format, 65535);
        std::vector<long> args;

        for(dword q = 0; q < aa.Size(); ++q)
            args.push_back(aa[q]);

        const std::string out = z30_format(format, args);

        if(!to_str)
        {
            // as printf: the console only; ZC_SCRIPT_PRINTF (test runs) puts it in the log
            static const bool printf_to_log = getenv("ZC_SCRIPT_PRINTF") != NULL;
            if(printf_to_log)
                al_trace("printf: %s", out.c_str());
        }
        else if(z30_set_string(dest, out) == SH::_Overflow)
            ri->d[2] = (long)ArrayH::strlen(dest) * 10000;
        else
            ri->d[2] = (long)out.size() * 10000;
        return true;
    }

    // -- items, enemies ------------------------------------------------------------------------------
    case Z30C_CURRENTITEMID:
    {
        const long ity = SH::read_stack(SPR + 1) / 10000, flags = SH::read_stack(SPR) / 10000;
        long id = -1;

        // (the jinx and bunny checks: this engine's current_item_id has no jinx immunity or
        // override items, and always leaves out what a bunny can't use)
        if(ity >= 0 && ity < 512)
            id = current_item_id((int)ity, (flags & 0x01) != 0);

        ri->d[2] = id * 10000;
        return true;
    }
    case Z30C_MODULEGETIC:
    {
        const long ptr = get_register(sarg1) / 10000, element = get_register(sarg2) / 10000;

        if((unsigned long)element > 511)
            Z_scripterrlog("Illegal itemclass supplied to ZInfo->GetItemClass().\nLegal values are 1 to 511.\n");
        else
            ArrayH::setArray(ptr, z30_itemclass_name((int)element).substr(0, 255));
        return true;
    }
    case Z30C_REPAIRSHIELD:
    {
        enemy *e = z30_npc_ref();
        const int bits = inv_front | inv_back | inv_left | inv_right;
        bool *shield = NULL;

        if(eFire *f = dynamic_cast<eFire *>(e)) shield = &f->shield;
        else if(eOther *o = dynamic_cast<eOther *>(e)) shield = &o->shield;
        else if(eStalfos *st = dynamic_cast<eStalfos *>(e)) shield = &st->shield;

        // (2.53's break_shield clears the shield flags, so the enemy's data says which it had)
        if(e && shield && !*shield && (e->d->flags & bits))
        {
            e->flags |= (e->d->flags & bits);
            *shield = true;

            if(get_bit(quest_rules, qr_BRKNSHLDTILES))
                e->o_tile = get_bit(quest_rules, qr_NEWENEMYTILES) ? e->d->e_tile : e->d->tile;
        }
        return true;
    }
    case Z30C_NPC_SUMMON_MINION_LAYER:
    {
        const long limit = get_register(sarg1) / 10000;
        enemy *e = z30_npc_ref();
        long ret = 0;

        if(e && q30_summon_layered_enemies(e, 1, (int)limit, false))
            ret = guys.spr(guys.Count() - 1)->getUID();

        set_register(sarg1, ret);
        return true;
    }

    // -- the switch hook from a script --------------------------------------------------------------
    case Z30C_SWITCHNPC:
    case Z30C_SWITCHITM:
    case Z30C_SWITCHLW:
    case Z30C_SWITCHEW:
    {
        const int effect = vbound((int)(get_register(sarg1) / 10000), 0, 255);
        set_register(sarg1, 0);
        bool ok = false;

        if(cmd == Z30C_SWITCHNPC)
        {
            if(enemy *e = z30_npc_ref())
                ok = q30_switchhook_script(effect, -1, e, NULL);
        }
        else
        {
            sprite *s = cmd == Z30C_SWITCHITM ? (sprite *)z30_item_ref() : cmd == Z30C_SWITCHLW ? (sprite *)z30_lwpn_ref() : (sprite *)z30_ewpn_ref();
            if(s)
                ok = q30_switchhook_script(effect, -1, NULL, s);
        }

        if(ok)
            set_register(sarg1, 10000);
        return true;
    }
    case Z30C_SWITCHCMB:
    {
        const long rpos = get_register(sarg1) / 10000;
        const int style = (int)(get_register(sarg2) / 10000);
        set_register(sarg1, 0);

        if(rpos < 0 || rpos >= q30_region_num_rpos())
            return true;

        const long pos = rpos - q30_region_rpos_of_pos(0);  // a position on the current screen

        if(pos >= 0 && pos < 176 && q30_switchhook_script(style, (int)pos, NULL, NULL))
            set_register(sarg1, 10000);
        return true;
    }

    // -- palettes ----------------------------------------------------------------------------------
    case Z30C_PALDATAWRITESPRITE:
        z30_paldata_write_sprite(false);
        return true;
    case Z30C_PALDATAWRITESPRITECS:
        z30_paldata_write_sprite(true);
        return true;

    // -- screens, regions, the viewport -------------------------------------------------------------
    case Z30C_ISSOLIDLAYER:
    {
        const long x = ri->d[0] / 10000, y = ri->d[1] / 10000, layer = ri->d[2] / 10000;

        if(BC::checkBounds(layer, 0, 6, "Screen->isSolidLayer()") != SH::_NoError)
            set_register(sarg1, 10000);
        else
            set_register(sarg1, z30_walkflag_layer((int)x, (int)y, (int)layer) ? 10000 : 0);
        return true;
    }
    case Z30C_REGION_LOAD_TMPSCR_FOR_LAYER_COMBO_POS:
    {
        const long layer = get_register(sarg1) / 10000, rpos = get_register(sarg2) / 10000;
        long ref = 0;

        // one screen of the region (upstream's TemporaryCurrentScreen): the region screen + 1 in bits 16-23
        if(BC::checkBounds(rpos, 0, q30_region_num_rpos() - 1, "Region->LoadTempScreenForComboPos()") == SH::_NoError &&
           BC::checkBounds(layer, 0, 6, "Region->LoadTempScreenForComboPos()") == SH::_NoError)
            ref = q30_region_on() ? (Z30_MAPDATA_TMP | ((rpos / 176 + 1) << 16) | layer) : (Z30_MAPDATA_TMP | layer);

        Z30_REF(MAPDATA) = ref;
        set_register(sarg1, ref);
        return true;
    }
    case Z30C_REGION_TRIGGER_SECRETS:
    {
        const long scr = get_register(sarg1) / 10000;
        const bool in_region = q30_region_on() ? q30_region_screen_index((int)scr) >= 0 : scr == currscr;

        if(!in_region)
        {
            Z_scripterrlog("Must use a screen in the current region. got: %ld\n", scr);
            return true;
        }

        if(scr == currscr)
            hidden_entrance(0, true, false, -4);
        else
            q30_region_trigger_secrets((int)scr);
        return true;
    }
    case Z30C_VIEWPORT_RESET_FOLLOW_SETTINGS:
        // (scripts can't override the camera's follow settings here: the DMap's or the quest's are used)
        return true;

    // -- messages, DMaps ------------------------------------------------------------------------------
    case Z30C_SETMESSAGE:
    {
        const long id = get_register(sarg1) / 10000;

        if(BC::checkMessage(id, "Game->SetMessage()") == SH::_NoError)
        {
            string text;
            ArrayH::getString(get_register(sarg2) / 10000, text, 8192);
            z30_msg_set(id, text);
        }
        return true;
    }
    case Z30C_SETDMAPNAME:
    case Z30C_SETDMAPTITLE:
    case Z30C_SETDMAPINTRO:
    {
        const long id = get_register(sarg1) / 10000, ptr = get_register(sarg2) / 10000;

        if(BC::checkDMapID(id, "Game->SetDMap...()") != SH::_NoError)
            return true;

        string s;

        if(cmd == Z30C_SETDMAPNAME)
        {
            ArrayH::getString(ptr, s, 22);
            strncpy(DMaps[id].name, s.c_str(), 21);
            DMaps[id].name[20] = 0;
        }
        else if(cmd == Z30C_SETDMAPINTRO)
        {
            ArrayH::getString(ptr, s, 73);
            strncpy(DMaps[id].intro, s.c_str(), 72);
            DMaps[id].intro[72] = 0;
        }
        else if(q30_quest_rule(Z30_QR_OLD_DMAP_INTRO_STRINGS) || !q30_dmap_title((int)id))
        {
            ArrayH::getString(ptr, s, 21);
            strncpy(DMaps[id].title, s.c_str(), 20);
            DMaps[id].title[20] = 0;

            if(q30_dmap_title((int)id))
                q30_set_dmap_texts((int)id, DMaps[id].title, q30_dmap_intro_string((int)id));
        }
        else
        {
            // the whole array, as the newer titles hold it
            ArrayH::getString(ptr, s, (word)vbound((long)ArrayH::getSize(ptr), 0L, 65535L));
            q30_set_dmap_texts((int)id, s.c_str(), q30_dmap_intro_string((int)id));
            strncpy(DMaps[id].title, s.c_str(), 20);
            DMaps[id].title[20] = 0;
        }
        return true;
    }

    case Z30C_SHOWF6SCREEN:
        z30_show_f6();
        return true;
    }

    return false;
}

// ==== registers =================================================================================

// FFScript::LongDistance (raw values)
static int32_t z30_long_distance(double x1, double y1, double x2, double y2)
{
    const double x = x1 - x2, y = y1 - y2;
    volatile double xx = x * x;
    volatile double yy = y * y;
    return (int32_t)sqrt(xx + yy);
}

// items a script set ForceGrab on (upstream's item::force_grab): Link takes them wherever they
// are, once he can (q30_msg.cpp, as the CollectItem string command's items)
static std::set<long> zasm30_forcegrab;
void q30_msg_collect_item(long uid);

bool q30_item_force_grab(long uid)
{
    return zasm30_forcegrab.count(uid) != 0;
}

// save_menu's fields (upstream's savemenu_get_register / _set_register and its arrays): -1 when
// the menu isn't valid; an array index past the options or texts reads 0.
static bool z30_savemenu_reg(const long arg, long &ret, const long value, const bool write)
{
    Q30SaveMenu *m = NULL;

    switch(arg)
    {
    case Z30R_SAVEMENU_CURSORTILE: case Z30R_SAVEMENU_CURSORCSET: case Z30R_SAVEMENU_BGCOLOR: case Z30R_SAVEMENU_SFX_CURSOR:
    case Z30R_SAVEMENU_SFX_CHOOSE: case Z30R_SAVEMENU_HSPACE: case Z30R_SAVEMENU_VSPACE: case Z30R_SAVEMENU_OPTX:
    case Z30R_SAVEMENU_OPTY: case Z30R_SAVEMENU_TEXT_ALIGN: case Z30R_SAVEMENU_TEXT_BOX_ALIGN: case Z30R_SAVEMENU_CLOSE_FRAMES:
    case Z30R_SAVEMENU_CLOSE_FLASH_RATE: case Z30R_SAVEMENU_MIDI: case Z30R_SAVEMENU_NUM_OPTIONS: case Z30R_SAVEMENU_FLAGS:
    case Z30R_SAVEMENU_OPT_FLAGS: case Z30R_SAVEMENU_OPT_COLOR_TEXT: case Z30R_SAVEMENU_OPT_COLOR_PICKED: case Z30R_SAVEMENU_OPT_FONTS:
    case Z30R_SAVEMENU_OPT_FRZSCR: case Z30R_SAVEMENU_ID: case Z30R_SAVEMENU_BGTILE: case Z30R_SAVEMENU_BGCSET:
    case Z30R_SAVEMENU_BGTW: case Z30R_SAVEMENU_BGTH: case Z30R_SAVEMENU_MUSIC: case Z30R_SAVEMENU_OPT_SHADOW_TYPE:
    case Z30R_SAVEMENU_OPT_COLOR_SHADOW: case Z30R_SAVEMENU_OPT_COLOR_SHADOW_PICKED: case Z30R_SAVEMENU_SEL_OPT_OFFSET_X:
    case Z30R_SAVEMENU_SEL_OPT_OFFSET_Y: case Z30R_SAVEMENU_NUM_MISCTEXTS: case Z30R_SAVEMENU_MISCTEXT_COLOR:
    case Z30R_SAVEMENU_MISCTEXT_SHADOW_TYPE: case Z30R_SAVEMENU_MISCTEXT_SHADOW_COLOR: case Z30R_SAVEMENU_MISCTEXT_FONT:
    case Z30R_SAVEMENU_MISCTEXT_X: case Z30R_SAVEMENU_MISCTEXT_Y: case Z30R_SAVEMENU_MISCTEXT_TEXT_ALIGN:
        m = z30_savemenu(Z30_REF(SAVEMENU));
        break;
    default:
        return false;
    }

    ret = -10000;

    if(!m)
        return true;

    const long v = value / 10000, idx = Z30_INDEX;
    int *field = NULL;
    long lo = 0, hi = 255;

    switch(arg)
    {
    case Z30R_SAVEMENU_CURSORTILE:
        if(write) m->cursor_tile = vbound(v, 0L, (long)NEWMAXTILES - 1);
        else ret = m->cursor_tile * 10000;
        return true;
    case Z30R_SAVEMENU_BGTILE:
        if(write) m->bg_tile = vbound(v, 0L, (long)NEWMAXTILES - 1);
        else ret = m->bg_tile * 10000;
        return true;
    case Z30R_SAVEMENU_CURSORCSET: field = &m->cursor_cset; hi = 0xF; break;
    case Z30R_SAVEMENU_BGCSET: field = &m->bg_cset; hi = 0xF; break;
    case Z30R_SAVEMENU_BGCOLOR: field = &m->bg_color; break;
    case Z30R_SAVEMENU_BGTW: field = &m->bg_tw; hi = 16; break;
    case Z30R_SAVEMENU_BGTH: field = &m->bg_th; hi = 16; break;
    case Z30R_SAVEMENU_SFX_CURSOR: field = &m->cursor_sfx; hi = WAV_COUNT - 1; break;
    case Z30R_SAVEMENU_SFX_CHOOSE: field = &m->choose_sfx; hi = WAV_COUNT - 1; break;
    case Z30R_SAVEMENU_HSPACE: field = &m->hspace; break;
    case Z30R_SAVEMENU_VSPACE: field = &m->vspace; break;
    case Z30R_SAVEMENU_OPTX: field = &m->opt_x; break;
    case Z30R_SAVEMENU_OPTY: field = &m->opt_y; break;
    case Z30R_SAVEMENU_TEXT_ALIGN: field = &m->text_align; hi = 2; break;
    case Z30R_SAVEMENU_TEXT_BOX_ALIGN: field = &m->textbox_align; hi = 2; break;
    case Z30R_SAVEMENU_CLOSE_FRAMES: field = &m->close_frames; hi = 65535; break;
    case Z30R_SAVEMENU_CLOSE_FLASH_RATE: field = &m->close_flash_rate; break;
    case Z30R_SAVEMENU_SEL_OPT_OFFSET_X: field = &m->opt_sel_x_offset; lo = -32768; hi = 32767; break;
    case Z30R_SAVEMENU_SEL_OPT_OFFSET_Y: field = &m->opt_sel_y_offset; lo = -32768; hi = 32767; break;

    case Z30R_SAVEMENU_MUSIC:   // a music object, raw
        if(write)
        {
            if(value == 0 || q30_music_exists(value))
                m->music = (int)value;
        }
        else
            ret = m->music;
        return true;
    case Z30R_SAVEMENU_MIDI:    // (no midi musics made here: reads 0 with no music, else what upstream gives none)
        if(!write)
            ret = m->music ? -10000 : 0;
        return true;
    case Z30R_SAVEMENU_NUM_OPTIONS:
        if(!write) ret = (long)m->options.size() * 10000;
        return true;
    case Z30R_SAVEMENU_NUM_MISCTEXTS:
        if(!write) ret = (long)m->misc_texts.size() * 10000;
        return true;
    case Z30R_SAVEMENU_ID:
        if(!write) ret = Z30_REF(SAVEMENU) * 10000;
        return true;
    case Z30R_SAVEMENU_FLAGS:
        if(idx < 0 || idx >= 5)
            ret = 0;
        else if(write)
            m->flags = value ? (m->flags | (1 << idx)) : (m->flags & ~(1 << idx));
        else
            ret = (m->flags & (1 << idx)) ? 10000 : 0;
        return true;

    default:
    {
        // the per-option and per-text arrays
        const bool opt = arg == Z30R_SAVEMENU_OPT_FLAGS || arg == Z30R_SAVEMENU_OPT_COLOR_TEXT || arg == Z30R_SAVEMENU_OPT_COLOR_PICKED
                         || arg == Z30R_SAVEMENU_OPT_FONTS || arg == Z30R_SAVEMENU_OPT_FRZSCR || arg == Z30R_SAVEMENU_OPT_SHADOW_TYPE
                         || arg == Z30R_SAVEMENU_OPT_COLOR_SHADOW || arg == Z30R_SAVEMENU_OPT_COLOR_SHADOW_PICKED;
        ret = 0;

        if(idx < 0 || idx >= (long)(opt ? m->options.size() : m->misc_texts.size()))
            return true;

        long *lfield = NULL;

        if(opt)
        {
            Q30SaveMenuOption &o = m->options[idx];
            switch(arg)
            {
            case Z30R_SAVEMENU_OPT_FLAGS: field = &o.flags; hi = 0xFFFF; break;
            case Z30R_SAVEMENU_OPT_COLOR_TEXT: field = &o.color; break;
            case Z30R_SAVEMENU_OPT_COLOR_PICKED: field = &o.picked_color; break;
            case Z30R_SAVEMENU_OPT_SHADOW_TYPE: field = &o.shadow_type; break;
            case Z30R_SAVEMENU_OPT_COLOR_SHADOW: field = &o.shadow_color; break;
            case Z30R_SAVEMENU_OPT_COLOR_SHADOW_PICKED: field = &o.picked_shadow_color; break;
            case Z30R_SAVEMENU_OPT_FRZSCR: field = &o.script; hi = 65535; break;
            case Z30R_SAVEMENU_OPT_FONTS: lfield = &o.font; break;
            }
        }
        else
        {
            Q30SaveMenuMiscText &t = m->misc_texts[idx];
            switch(arg)
            {
            case Z30R_SAVEMENU_MISCTEXT_COLOR: field = &t.color; break;
            case Z30R_SAVEMENU_MISCTEXT_SHADOW_TYPE: field = &t.shadow_type; break;
            case Z30R_SAVEMENU_MISCTEXT_SHADOW_COLOR: field = &t.shadow_color; break;
            case Z30R_SAVEMENU_MISCTEXT_X: field = &t.x; break;
            case Z30R_SAVEMENU_MISCTEXT_Y: field = &t.y; break;
            case Z30R_SAVEMENU_MISCTEXT_TEXT_ALIGN: field = &t.text_align; hi = 2; break;
            case Z30R_SAVEMENU_MISCTEXT_FONT: lfield = &t.font; break;
            }
        }

        if(lfield)
        {
            if(write) *lfield = v;
            else ret = *lfield * 10000;
            return true;
        }
        break;
    }
    }

    if(field)
    {
        if(write)
            *field = (int)vbound(v, lo, hi);
        else
            ret = *field * 10000L;
    }
    return true;
}

// items' move flags from their data (upstream's item constructor: moveflags = itemdata's), for
// zasm30_item_state to give a new item sprite's script state
static std::map<int, long> zasm30_item_moveflags;

void q30_set_item_moveflags(int id, long flags)
{
    if(id < 0)
        zasm30_item_moveflags.clear();
    else
        zasm30_item_moveflags[id] = flags;
}

long q30_item_moveflags_of(int id)
{
    std::map<int, long>::const_iterator f = zasm30_item_moveflags.find(id);
    return f != zasm30_item_moveflags.end() ? f->second : 3;    // move_obeys_grav | move_can_pitfall
}

static bool zasm30_sys_reg(const long arg, long &ret, const long value, const bool write)
{
    if(z30_savemenu_reg(arg, ret, value, write))
        return true;

    switch(arg)
    {
    case Z30R_CREATELWPNDX:
    {
        if(write)
            return true;

        const long id = ri->d[0] / 10000;

        if(Lwpns.Count() < 256)
        {
            Lwpns.add(new weapon((fix)0, (fix)0, (fix)0, (int)id, 0, 0, 0, -1, Link.getUID()));
            ri->lwpn = Lwpns.spr(Lwpns.Count() - 1)->getUID();
        }
        else
        {
            Z_scripterrlog("Tried to create too many LWeapons on the screen. The current LWeapon count is: %d\n", Lwpns.Count());
            ri->lwpn = 0;
        }

        ret = ri->lwpn;
        return true;
    }

    case Z30R_LONGDISTANCE:
    case Z30R_LONGDISTANCESCALE:
    {
        if(write)
            return true;

        const double x1 = (double)Z30_D(6), y1 = (double)Z30_D(0), x2 = (double)Z30_D(1), y2 = (double)Z30_D(2);

        if(arg == Z30R_LONGDISTANCE)
            ret = z30_long_distance(x1, y1, x2, y2);
        else
        {
            int32_t scale = (int32_t)Z30_D(7);
            if(!scale)
                scale = 1;
            const double x3 = x1 + (x2 - x1) / scale, y3 = y1 + (y2 - y1) / scale;
            ret = (int32_t)((int64_t)z30_long_distance(x1, y1, x3, y3) * scale);
        }
        return true;
    }

    case Z30R_BUTTONHELD:
    {
        const int n = vbound((int)Z30_INDEX, 0, 18 - 1);
        if(write)
            button_hold[n] = value != 0;
        else
            ret = button_hold[n] ? 10000 : 0;
        return true;
    }

    case Z30R_DROPSETITEMS:
    case Z30R_DROPSETCHANCES:
    {
        const long ds = Z30_REF(DROPSETDATA), idx = Z30_INDEX;

        if(unsigned(ds) >= MAXITEMDROPSETS || unsigned(idx) >= 10)
        {
            ret = -10000;
            return true;
        }

        item_drop_object &d = item_drop_sets[ds];

        if(arg == Z30R_DROPSETITEMS)
        {
            if(write)
                d.item[idx] = (word)vbound(value / 10000, 0L, (long)Q30_MAXITEMS - 1);
            else
                ret = d.item[idx] * 10000;
        }
        else if(write)
            d.chance[idx + 1] = (word)vbound(value / 10000, 0L, 32767L);
        else
            ret = d.chance[idx + 1] * 10000;
        return true;
    }

    case Z30R_ITEMGRAVITY:
    {
        item *it = z30_item_ref();
        Zasm30SpriteState *st = it ? zasm30_item_state(it) : NULL;

        if(write)
        {
            if(st)
                st->move_flags = value ? (st->move_flags | 1) : (st->move_flags & ~1L);
        }
        else
            ret = (st && (st->move_flags & 1)) ? 10000 : 0;
        return true;
    }

    case Z30R_ITEMFORCEGRAB:
    {
        item *it = z30_item_ref();

        if(!it)
            ret = 0;
        else if(!write)
            ret = zasm30_forcegrab.count(it->getUID()) ? 10000 : 0;
        else if(value)
        {
            if(zasm30_forcegrab.insert(it->getUID()).second)
                q30_msg_collect_item(it->getUID());
        }
        else
            zasm30_forcegrab.erase(it->getUID());
        return true;
    }

    case Z30R_SUBDATATRANSARGS:
        return z30_sub_trans_args(write, ret, value);

    case Z30R_GAME_SAVEMENU_F6:         // Game->SaveMenuF6 / SaveMenuGameOver: the menus F6 and the game over open
    case Z30R_GAME_SAVEMENU_GAMEOVER:
    {
        int &v = arg == Z30R_GAME_SAVEMENU_F6 ? zasm30_savemenu_f6 : zasm30_savemenu_game_over;
        if(write)
            v = vbound((int)(value / 10000), 0, (int)Q30_NUM_SAVE_MENUS);
        else
            ret = v * 10000;
        return true;
    }

    case Z30R_WEBSOCKET_STATE:
    case Z30R_WEBSOCKET_HAS_MESSAGE:
    case Z30R_WEBSOCKET_MESSAGE_TYPE:
    case Z30R_WEBSOCKET_URL:
    {
        Z30WebSocket *ws = z30_websocket(Z30_REF(WEBSOCKET));
        ret = 0;

        if(write || !ws)
            return true;

        if(arg == Z30R_WEBSOCKET_STATE)
            ret = 0;        // WebSocketStatus::Connecting (raw), as it stays with no network
        else if(arg == Z30R_WEBSOCKET_URL)
            ret = (Z30_INDEX >= 0 && Z30_INDEX < (long)ws->url.size()) ? (long)(byte)ws->url[Z30_INDEX] * 10000 : 0;
        return true;
    }
    }

    return false;
}

static bool zasm30_sys_get(const long arg, long &ret)
{
    return zasm30_sys_reg(arg, ret, 0, false);
}

static bool zasm30_sys_set(const long arg, const long value)
{
    long unused = 0;
    return zasm30_sys_reg(arg, unused, value, true);
}

#endif

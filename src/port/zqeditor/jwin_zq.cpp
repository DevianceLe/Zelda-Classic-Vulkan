// zqeditor: what ZQuest needs from jwin beyond the dialog procedures in
// src/port/game/jwin_port.cpp. ZQuest's main screen and its full-screen pages
// are drawn in software, so they keep small drawing helpers (bevelled frames,
// buttons, title bars); context menus are shown by the dialog host.
//
// Written from scratch for zcport (no jwin or Allegro code).

#define NOMINMAX

#include "precompiled.h" //always first

#include "jwin.h"
#include "ui_dialogs_internal.h"

#include <string.h>

using namespace zcui_dlg;

extern int scheme[];

unsigned int col_diff[3 * 128];

namespace
{

inline int shade(int index)
{
    return palette_color[scheme[index]];
}

inline void hrun(BITMAP *dest, int x1, int y, int x2, int c)
{
    rectfill(dest, x1, y, x2, y, c);
}

inline void vrun(BITMAP *dest, int x, int y1, int y2, int c)
{
    rectfill(dest, x, y1, x, y2, c);
}

} // namespace

// Weighted squares of color differences, indexed by channel and difference.
void bestfit_init(void)
{
    col_diff[0] = col_diff[128] = col_diff[256] = 0;

    for(int i = 1; i < 64; ++i)
    {
        int k = i * i;
        col_diff[0 + i] = col_diff[0 + 128 - i] = k * (59 * 59);
        col_diff[128 + i] = col_diff[128 + 128 - i] = k * (30 * 30);
        col_diff[256 + i] = col_diff[256 + 128 - i] = k * (11 * 11);
    }
}

// Flat, like the dialogs the host draws: an outline and no light or shadow
// edges. The corner pixels are left out, which softens the corners.
void jwin_draw_frame(BITMAP *dest, int x, int y, int w, int h, int style)
{
    int edge, inner;

    switch(style)
    {
    case FR_INV:
        edge = jcDARK; inner = jcMEDDARK;
        break;
    case FR_DEEP:
    case FR_DARK:
        edge = jcDARK; inner = jcDARK;
        break;
    case FR_BOX:
    case FR_ETCHED:
    case FR_MEDDARK:
    case FR_WIN:
    default:
        edge = jcMEDDARK; inner = jcBOX;
        break;
    }

    hrun(dest, x + 1, y, x + w - 2, shade(edge));
    hrun(dest, x + 1, y + h - 1, x + w - 2, shade(edge));
    vrun(dest, x, y + 1, y + h - 2, shade(edge));
    vrun(dest, x + w - 1, y + 1, y + h - 2, shade(edge));
    hrun(dest, x + 1, y + 1, x + w - 2, shade(inner));
    hrun(dest, x + 1, y + h - 2, x + w - 2, shade(inner));
    vrun(dest, x + 1, y + 2, y + h - 3, shade(inner));
    vrun(dest, x + w - 2, y + 2, y + h - 3, shade(inner));
}

void jwin_draw_win(BITMAP *dest, int x, int y, int w, int h, int frame)
{
    rectfill(dest, x + 2, y + 2, x + w - 3, y + h - 3, shade(jcBOX));
    jwin_draw_frame(dest, x, y, w, h, frame);
}

// state 0: raised, 1: pressed, 2: dark, 3: medium dark; type 1: window edge
void jwin_draw_button(BITMAP *dest, int x, int y, int w, int h, int state, int type)
{
    static const int by_state[4] = { FR_BOX, FR_INV, FR_DARK, FR_MEDDARK };
    int frame = (state == 0 && type == 1) ? (int)FR_WIN : by_state[state & 3];
    jwin_draw_win(dest, x, y, w, h, frame);

    if(state != 0)      // a pressed or selected button is filled darker
        rectfill(dest, x + 1, y + 1, x + w - 2, y + h - 2, shade(jcMEDDARK));
}

void dotted_rect(BITMAP *dest, int x1, int y1, int x2, int y2, int fg, int bg)
{
    int phase = (x1 + y1) & 1;

    for(int x = x1; x <= x2; ++x)
    {
        putpixel(dest, x, y1, (((x + y1) & 1) == phase) ? fg : bg);
        putpixel(dest, x, y2, (((x + y2) & 1) == phase) ? fg : bg);
    }

    for(int y = y1 + 1; y < y2; ++y)
    {
        putpixel(dest, x1, y, (((y + x1) & 1) == phase) ? fg : bg);
        putpixel(dest, x2, y, (((y + x2) & 1) == phase) ? fg : bg);
    }
}

void draw_x_button(BITMAP *dest, int x, int y, int state)
{
    int c = palette_color[scheme[jcBOXFG]];
    jwin_draw_button(dest, x, y, 16, 14, state, 0);
    int ox = x + 4 + (state ? 1 : 0);
    int oy = y + 3 + (state ? 1 : 0);

    for(int thick = 0; thick < 2; ++thick)
    {
        line(dest, ox + thick, oy, ox + 6 + thick, oy + 6, c);
        line(dest, ox + thick, oy + 6, ox + 6 + thick, oy, c);
    }
}

char *shorten_string(char *dest, char *src, FONT *usefont, int maxchars, int maxwidth)
{
    strncpy(dest, src, maxchars);
    dest[maxchars - 1] = 0;
    int len = (int)strlen(dest);

    // trade the end of the text for "..." until it fits
    while(text_length(usefont, dest) > maxwidth && len > 4)
    {
        strcpy(dest + len - 4, "...");
        --len;
    }

    return dest;
}

void jwin_draw_titlebar(BITMAP *dest, int x, int y, int w, int h, const char *str, bool draw_button)
{
    rectfill(dest, x, y, x + w - 1, y + h - 1, shade(jcTITLEL));
    char buf[512];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    int len = (int)strlen(buf);

    while(text_length(font, buf) > w - 20 && len > 4)
    {
        strcpy(buf + len - 4, "...");
        --len;
    }

    textout_ex(dest, font, buf, x + 2, y + (h - text_height(font)) / 2, shade(jcTITLEFG), -1);

    if(draw_button)
        draw_x_button(dest, x + w - 18, y + 2, 0);
}

namespace
{

void button_face(BITMAP *dest, int x, int y, int w, int h, int flags)
{
    if(flags & D_SELECTED)
        jwin_draw_button(dest, x, y, w, h, 2, 0);
    else if(flags & D_GOTFOCUS)
    {
        rect(dest, x, y, x + w - 1, y + h - 1, shade(jcDARK));
        jwin_draw_button(dest, x + 1, y + 1, w - 2, h - 2, 0, 0);
    }
    else
        jwin_draw_button(dest, x, y, w, h, 0, 0);
}

} // namespace

void jwin_draw_text_button(BITMAP *dest, int x, int y, int w, int h, const char *str, int flags, bool show_dotted_rect)
{
    int press = (flags & D_SELECTED) ? 1 : 0;
    int ty = y + h / 2 - text_height(font) / 2;
    button_face(dest, x, y, w, h, flags);

    if(flags & D_DISABLED)
    {
        gui_textout_ex(dest, str, x + w / 2, ty, shade(jcMEDDARK), -1, TRUE);
    }
    else
        gui_textout_ex(dest, str, x + w / 2 + press, ty + press, shade(jcBOXFG), -1, TRUE);

    (void)show_dotted_rect;     // button_face() marks the focus
}

void jwin_draw_graphics_button(BITMAP *dest, int x, int y, int w, int h, BITMAP *bmp, BITMAP *bmp2, int flags, bool show_dotted_rect, bool overlay)
{
    int press = (flags & D_SELECTED) ? 1 : 0;
    button_face(dest, x, y, w, h, flags);
    BITMAP *face = (flags & D_DISABLED) ? bmp2 : bmp;

    if(face)
    {
        int fx = x + w / 2 - face->w / 2 + press;
        int fy = y + h / 2 - face->h / 2 + press;

        if(overlay)
            masked_blit(face, dest, 0, 0, fx, fy, face->w, face->h);
        else
            blit(face, dest, 0, 0, fx, fy, face->w, face->h);
    }

    (void)show_dotted_rect;     // button_face() marks the focus
}

void draw_arrow_button(BITMAP *dest, int x, int y, int w, int h, int up, int state)
{
    int size = h / 3 < 5 ? h / 3 : 5;
    jwin_draw_button(dest, x, y, w, h, state, 1);
    int cx = x + w / 2 - (state ? 0 : 1);
    int top = y + (h - size) / 2 + (state ? 1 : 0);

    for(int i = 0; i < size; ++i)
    {
        int half = up ? i : size - i - 1;
        hrun(dest, cx - half, top + i, cx + half, palette_color[scheme[jcBOXFG]]);
    }
}

void draw_arrow_button_horiz(BITMAP *dest, int x, int y, int w, int h, int up, int state)
{
    int size = w / 3 < 5 ? w / 3 : 5;
    jwin_draw_button(dest, x, y, w, h, state, 1);
    int cy = y + h / 2 - (state ? 0 : 1);
    int left = x + (w - size) / 2 + (state ? 1 : 0);

    for(int i = 0; i < size; ++i)
    {
        int half = up ? i : size - i - 1;
        vrun(dest, left + i, cy - half, cy + half, palette_color[scheme[jcBOXFG]]);
    }
}

// Multi-line text; '&' underlines the next letter, "&&" is a '&'. pos: 0 left,
// 1 centered, 2 right. Returns the widest line in pixels; a NULL bitmap only
// measures.
int gui_textout_ln(BITMAP *bmp, FONT *f, unsigned char *s, int x, int y, int color, int bg, int pos)
{
    int widest = 0;
    int line_y = y;

    while(*s)
    {
        char row[1024];
        int len = 0, underline = -1;

        for(; *s && *s != '\n' && len < (int)sizeof(row) - 1; ++s)
        {
            if(*s == '&')
            {
                if(s[1] != '&')
                {
                    underline = len;
                    continue;
                }
                ++s;
            }
            row[len++] = (char)*s;
        }

        row[len] = 0;

        if(*s == '\n')
            ++s;

        int width = text_length(f, row);
        if(width > widest)
            widest = width;

        int left = x - (pos == 1 ? widest / 2 : (pos == 2 ? widest : 0));

        if(bmp)
        {
            textout_ex(bmp, f, row, left, line_y, color, bg);

            if(underline >= 0 && underline < len)
            {
                char keep = row[underline + 1];
                row[underline + 1] = 0;
                int end = text_length(f, row);
                row[underline] = 0;
                int start = text_length(f, row);
                row[underline + 1] = keep;
                int under = line_y + text_height(f) - 1;
                hrun(bmp, left + start, under, left + end - 1, color == -1 ? bg : color);
            }
        }

        line_y += text_height(f);
    }

    return widest;
}

// ---------------------------------------------------------------------------
// context menus
// ---------------------------------------------------------------------------

namespace
{

int popup_marker(int msg, DIALOG *d, int c)
{
    (void)d;
    (void)c;
    return msg == MSG_ZCUI_KIND ? (KIND_MAGIC | K_POPUP) : D_O_K;
}

} // namespace

// Returns the index of the top-level item chosen, or -1. Item callbacks run
// as usual (the host calls them).
int jwin_do_menu(MENU *menu, int x, int y)
{
    (void)x;
    (void)y;
    DIALOG dlg[2];
    memset(dlg, 0, sizeof(dlg));
    dlg[0].proc = popup_marker;
    dlg[0].flags = D_EXIT;
    dlg[0].dp = menu;

    popup_chosen = NULL;
    do_dialog(dlg, -1);

    if(popup_chosen)
        for(int i = 0; menu[i].text; ++i)
            if(&menu[i] == popup_chosen)
                return i;

    return -1;
}

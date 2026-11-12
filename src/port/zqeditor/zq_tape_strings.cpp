// The STR section of 2.55 and 3.0 Nightly quests (versions 5-13) as a tape (zq_tape.h), string
// by string as q255_read_strings reads it, each field keyed "msg<n>.<field>".
//
// The text, next string, frame, position, size, spacing, flags, sound and list position are
// 2.53's: its string editor edits them and write_strings_255 writes them. The rest is 2.55 / 3.0
// only ("extra"): text margins, the portrait, the shadow, the draw layer, and from version 13
// the more / scroll icons, scroll speeds and menu sounds.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>

#include "zq_tape.h"

bool zq_tape_strings(TapeParser &p, int v)
{
    const int count = (int)p.hidden("count", 2);
    char k[48], l[64];

    for(int i = 0; i < count && !p.bad; ++i)
    {
        p.extra(false);
        p.group("String");
        snprintf(k, sizeof(k), "msg%d.text", i);

        if(v >= 9)
            p.lstr(k, "Text");
        else
            p.text(k, "Text", MSGSIZE + 1);

#define F(name) (snprintf(k, sizeof(k), "msg%d." name, i), k)
        p.u(F("next"), "Next string", 2);
        p.s(F("tile"), "Frame tile", v >= 6 ? 4 : 2);
        p.u(F("cset"), "Frame CSet", 1);
        p.u(F("trans"), "Transparent", 1);
        p.u(F("font"), "Font", 1);
        p.s(F("x"), "X", 2);
        p.s(F("y"), "Y", 2);
        p.u(F("w"), "Width", 2);
        p.u(F("h"), "Height", 2);
        p.u(F("hspace"), "Horizontal spacing", 1);
        p.u(F("vspace"), "Vertical spacing", 1);
        p.u(F("flags"), "Flags", 1);

        if(v >= 7)
        {
            p.extra(true);
            p.group("Layout");
            p.num(F("margin_top"), "Text margin: top", 1, false, TK_INT, TL_NONE, 0,
                  "Pixels between the frame's top edge and the text.");
            p.num(F("margin_bottom"), "Text margin: bottom", 1, false, TK_INT, TL_NONE, 0,
                  "Pixels between the text and the frame's bottom edge.");
            p.num(F("margin_left"), "Text margin: left", 1, false, TK_INT, TL_NONE, 0,
                  "Pixels between the frame's left edge and the text.");
            p.num(F("margin_right"), "Text margin: right", 1, false, TK_INT, TL_NONE, 0,
                  "Pixels between the text and the frame's right edge.");
            p.group("Portrait");
            p.s(F("portrait_tile"), "Portrait tile", 4);
            p.u(F("portrait_cset"), "Portrait CSet", 1);
            p.u(F("portrait_x"), "Portrait X", 1);
            p.u(F("portrait_y"), "Portrait Y", 1);
            p.num(F("portrait_tw"), "Portrait width (tiles)", 1, false, TK_INT, TL_NONE, 0, "0: no portrait.");
            p.num(F("portrait_th"), "Portrait height (tiles)", 1, false, TK_INT, TL_NONE, 0, "0: no portrait.");
        }

        if(v >= 8)
        {
            p.extra(true);
            p.group("Layout");
            p.num(F("shadow_type"), "Text shadow type", 1, false, TK_INT, TL_NONE, 0,
                  "0 none, 1 shadow, 2 shadow (U), 3 shadow (O), 4 shadow (+), 5 shadow (X), 6 shadowed, "
                  "7 shadowed (U), 8 shadowed (O), 9 shadowed (+), 10 shadowed (X).");
            p.u(F("shadow_color"), "Text shadow color", 1);
        }

        if(v >= 10)
        {
            p.extra(true);
            p.group("Layout");
            p.num(F("layer"), "Draw layer", 1, false, TK_INT, TL_NONE, 0, "The layer the string is drawn on (6: above layer 6).");
        }

        p.extra(false);
        p.group("String");
        p.u(F("sfx"), "Sound", v >= 12 ? 2 : 1, TL_SFX);
        p.u(F("listpos"), "List position", 2);

        if(v >= 13)
        {
            p.extra(true);
            p.group("Icons");
            static const char *const icons[3][2] = { { "more", "'More' icon" }, { "up", "Scroll-up icon" }, { "down", "Scroll-down icon" } };

            for(int q = 0; q < 3; ++q)
            {
                snprintf(k, sizeof(k), "msg%d.icon_%s_sprite", i, icons[q][0]);
                snprintf(l, sizeof(l), "%s: sprite", icons[q][1]);
                p.u(k, l, 2, TL_SPRITE);
                snprintf(k, sizeof(k), "msg%d.icon_%s_anchor", i, icons[q][0]);
                snprintf(l, sizeof(l), "%s: anchor", icons[q][1]);
                p.u(k, l, 1);
                snprintf(k, sizeof(k), "msg%d.icon_%s_x", i, icons[q][0]);
                snprintf(l, sizeof(l), "%s: X", icons[q][1]);
                p.s(k, l, 2);
                snprintf(k, sizeof(k), "msg%d.icon_%s_y", i, icons[q][0]);
                snprintf(l, sizeof(l), "%s: Y", icons[q][1]);
                p.s(k, l, 2);
            }

            p.group("Scrolling and sounds");
            p.u(F("scroll_active"), "Scroll speed (while held)", 1);
            p.u(F("scroll_passive"), "Scroll speed", 1);
            p.u(F("sfx_advance"), "Advance sound", 2, TL_SFX);
            p.u(F("sfx_menu_move"), "Menu cursor sound", 2, TL_SFX);
            p.u(F("sfx_menu_close"), "Menu close sound", 2, TL_SFX);
        }
#undef F
    }

    return !p.bad;
}

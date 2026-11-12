// Saving a quest made in 2.55 as 2.55.17 or 3.0 Nightly (zqn_255.h): the strings (STR).
//
// Read with the tape (zq_tape_strings, versions 5-13) the way readstrings reads them, then
// written as the target's newest version: 2.55.17's 10, 3.0's 13. What a version lacks takes
// init_msgstr's values: margins 8, 0, 8, 8, an empty 1x1 portrait, no shadow, layer 6.
//
// 3.0 also:
// - saves text older than version 11 (2.55's binary control codes) as text with \Name\arg\
//   codes (zqn_ascii_text, ported from scc.cpp in zqn_strings.cpp);
// - clears flags 0x04 and 0x08, defined once but never used;
// - gives every string the init data's "more" icon (sprite 43, x, y no lower than 160, anchored
//   to the screen or with y an offset), as its init data reader does for init data older than
//   version 49; no scroll icons (sprite 0xFFFF, anchor 7), scroll speeds of 1, no advance or
//   close sound, and the string's own sound for the menu cursor.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_writers.h"
#include "zq_tape.h"
#include "zqn_255.h"

std::string zqn_ascii_text(const std::string &legacy);     // zqn_strings.cpp

namespace
{

struct MoreIcon
{
    int x, y;
    bool offset;
};

// The more icon from the quest's init data (its own-version section, as now).
bool more_icon(int target, MoreIcon &m)
{
    m.x = 224;
    m.y = 64;
    m.offset = false;
    ZqNewerContext ic;
    ic.target = target;
    ic.src_zelda_version = 0x255;
    ic.src_build = 0;
    ic.src_part = zq_find_part(ID_INITDATA);
    ic.src_version = ic.src_cversion = 0;

    if(!ic.src_part)
        return true;

    zq_part_versions(*ic.src_part, &ic.src_version, &ic.src_cversion);
    Zqn255Section s;

    if(!zqn255_section(ic, s))
        return false;

    if(s.version >= 49)
        return true;    // the icons are the strings' own from then on

    Tape t;
    TapeParser p(s.data.empty() ? NULL : &s.data[0], s.data.size(), 0, &t);

    if(!zq_tape_init(p, s.version) || p.bad)
        return false;

    m.x = (int)t.get("msg_more_x", 224);
    m.y = (int)t.get("msg_more_y", 64);
    m.offset = t.get("msg_more_is_offset", 0) != 0;
    return true;
}

std::string c_text(const TapeField *f)
{
    if(!f)
        return std::string();

    size_t n = 0;

    while(n < f->bytes.size() && f->bytes[n])
        ++n;

    return std::string((const char *)(f->bytes.empty() ? NULL : &f->bytes[0]), n);
}

int write_strings(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version < 5 || ctx.src_version > 12)
        return ZQW_UNSUPPORTED;

    const bool t30 = ctx.target == ZQ_NEWER_30;

    if(!t30 && ctx.src_version > 10)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    Tape t;
    TapeParser p(s.data.empty() ? NULL : &s.data[0], s.data.size(), 0, &t);

    if(!zq_tape_strings(p, s.version) || p.bad || p.pos != s.data.size())
    {
        zqw_last_error = "its strings aren't laid out as expected";
        return 2;
    }

    MoreIcon more;

    if(t30 && !more_icon(ctx.target, more))
    {
        zqw_last_error = "its init data (the strings' 'more' icon) isn't laid out as expected";
        return 2;
    }

    const int v = s.version;
    const int count = (int)t.get("count");
    const TapeIndex ix(t);
    Zqn255Out o;
    o.w(count);
    char k[48];

    for(int i = 0; i < count; ++i)
    {
#define G(name, dflt) (snprintf(k, sizeof(k), "msg%d." name, i), ix.get(k, dflt))
        snprintf(k, sizeof(k), "msg%d.text", i);
        std::string text = c_text(ix.find(k));

        if(t30 && v < 11)
            text = zqn_ascii_text(text);

        if(text.size() > 8192)
            text.resize(8192);

        o.l((int64_t)text.size());
        o.put(text.data(), text.size());
        o.w(G("next", 0));
        o.l(G("tile", 0));
        o.c(G("cset", 0));
        o.c(G("trans", 0) ? 1 : 0);
        o.c(G("font", 0));
        o.w(G("x", 0));
        o.w(G("y", 0));
        o.w(G("w", 0));
        o.w(G("h", 0));
        o.c(G("hspace", 0));
        o.c(G("vspace", 0));
        int64_t flags = G("flags", 0);
        o.c(t30 ? (flags & ~0x0C) : flags);
        o.c(G("margin_top", 8));
        o.c(G("margin_bottom", 0));
        o.c(G("margin_left", 8));
        o.c(G("margin_right", 8));
        o.l(G("portrait_tile", 0));
        o.c(G("portrait_cset", 0));
        o.c(G("portrait_x", 0));
        o.c(G("portrait_y", 0));
        o.c(G("portrait_tw", 1));
        o.c(G("portrait_th", 1));
        o.c(G("shadow_type", 0));
        o.c(G("shadow_color", 0));
        o.c(G("layer", 6));
        const int64_t sfx = G("sfx", 0);

        if(t30)
            o.w(sfx);
        else
            o.c(sfx);

        o.w(G("listpos", i));

        if(t30)
        {
            o.w(43);                    // iwMore
            o.c(more.offset ? 10 : 9);  // screen_y_offset, screen
            o.w(more.x);
            o.w(more.y < 160 ? more.y : 160);

            for(int q = 0; q < 2; ++q)  // scroll up and down: none
            {
                o.w(0xFFFF);
                o.c(7);
                o.w(0);
                o.w(0);
            }

            o.c(1);
            o.c(1);
            o.w(0);
            o.w(sfx);
            o.w(0);
        }
#undef G
    }

    std::vector<byte> none;
    return t30 ? zqn255_emit(f, ID_STRINGS, 13, 13, none, o.b) : zqn255_emit(f, ID_STRINGS, 10, 2, none, o.b);
}

ZqNewerRegistration g_reg_strings(ID_STRINGS, write_strings, "2.55 strings");

} // namespace

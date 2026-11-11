// The strings (STR) of a quest made in 2.53.1 or older, saved as 2.55.17 (section version 10)
// or 3.0 Nightly (version 13). See zq_writers.h for what a newer-version writer does.
//
// 2.55 keeps the text as 2.53 does: characters, with string control codes as binary bytes
// (the code plus one, then each argument plus one, 255 introducing a second byte). 3.0 reads
// that "legacy" text and saves it as text: a control code becomes \Name\arg\...\ and a
// backslash or brace in the text is escaped (components/scc/scc.cpp, ported below).
//
// The fields 2.53 hasn't take the reader's defaults: margins 8, 0, 8, 8, an empty 1x1
// portrait, no shadow, layer 6, and in 3.0 scroll speeds of 1, no advance or close sound and
// the menu sound the string's own. The width and height default for strings older than
// section version 5 depends on qr_STRING_FRAME_OLD_WIDTH_HEIGHT, which upstream's rules
// reader turns on for every quest older than 2.55, giving 2.53's own 192x24.
//
// 3.0 keeps the "more" icon in each string. Older init data held one for the whole quest
// (x, y, and whether y is an offset), and upstream's init data reader gives it to every
// string: sprite 43 (iwMore), anchored to the screen or, with the offset, to the screen's x
// only, with y no lower than 160.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "zq_writers.h"
#include "zqn_edit_test.h"

extern MsgStr *MsgStrings;
extern word msg_count;
extern zinitdata zinit;

namespace
{

struct Buf
{
    std::vector<byte> b;

    void c(long v) { b.push_back((byte)v); }
    void w(long v) { c(v); c(v >> 8); }
    void l(long v) { c(v); c(v >> 8); c(v >> 16); c(v >> 24); }
    void put(const void *p, size_t n) { const byte *q = (const byte *)p; b.insert(b.end(), q, q + n); }
};

// ---- string control codes (scc.cpp) ------------------------------------------------------

struct Command
{
    int code;
    const char *name;
    int args;           // what the legacy encoding stores (the minimum for optional ones)
};

const Command g_commands[] =
{
    { 1, "TextColor", 2 }, { 2, "Speed", 1 }, { 3, "GoIfScreenD", 3 }, { 4, "GoIfRand", 2 },
    { 5, "GoIfItem", 2 }, { 6, "GoIfCounter", 3 }, { 7, "GoIfCounter%", 3 }, { 8, "GoIfMcGuffin", 2 },
    { 9, "GoIfMcGuffinCount", 2 }, { 10, "CounterAdd", 2 }, { 11, "CounterSubtract", 2 },
    { 12, "CounterSet", 2 }, { 13, "CounterAdd%", 2 }, { 14, "CounterSubtract%", 2 },
    { 15, "CounterSet%", 2 }, { 16, "GiveItem", 1 }, { 17, "TakeItem", 1 }, { 18, "Warp", 6 },
    { 19, "SetScreenD", 4 }, { 20, "SFX", 1 }, { 21, "MIDI", 1 }, { 22, "Name", 0 },
    { 23, "GoIfAnyScreenD", 5 }, { 24, "Portrait", 6 }, { 25, "Newline", 0 }, { 26, "ShadowColor", 2 },
    { 27, "ShadowType", 1 }, { 28, "Tile", 5 }, { 29, "EndString", 0 }, { 30, "WaitAdvance", 0 },
    { 128, "SetupMenu", 5 }, { 129, "MenuChoice", 5 }, { 130, "RunMenu", 0 }, { 131, "GoIfMenuChoice", 2 },
    { 132, "TriggerSecrets", 1 }, { 133, "SetScreenState", 2 }, { 134, "SetAnyScreenState", 4 },
    { 135, "Font", 1 }, { 136, "RunFrozenGenericScript", 2 }, { 137, "TriggerComboCopycat", 1 },
    { 138, "GoIfGlobalState", 3 }, { 139, "GoIfScreenState", 3 }, { 140, "GoIfAnyScreenState", 5 },
    { 141, "GoIfScreenExState", 3 }, { 142, "GoIfAnyScreenExState", 5 }, { 143, "GoIfLevelState", 4 },
    { 144, "GoIfLevelItem", 4 }, { 145, "GoIfSecrets", 1 }, { 146, "SetScreenExState", 2 },
    { 147, "SetAnyScreenExState", 4 }, { 148, "SetGlobalState", 2 }, { 149, "SetLevelState", 3 },
    { 150, "SetLevelItem", 3 }, { 151, "Counter", 1 }, { 152, "MaxCounter", 1 }, { 153, "KillHero", 1 },
    { 154, "CollectItem", 1 }, { 155, "Delay", 1 }, { 156, "ForceDelay", 1 }, { 157, "GoIfBottle", 3 },
    { 158, "ChangeBottle", 3 }, { 159, "Music", 1 }, { 160, "MusicRefresh", 1 }, { 161, "SetCurrentScreenD", 2 },
};

const Command *find_command(int code)
{
    for(size_t i = 0; i < sizeof(g_commands) / sizeof(g_commands[0]); ++i)
        if(g_commands[i].code == code)
            return &g_commands[i];

    return NULL;
}

// One argument of a legacy command after the byte at i (parse_legacy_binary_argument): the
// value plus one, and if 255 follows, a second byte worth 254 each. False at the string's end.
bool legacy_argument(const std::string &s, size_t &i, long &value)
{
    if(i + 1 >= s.size())
        return false;

    byte v = (byte)((byte)s[++i] - 1);
    value = v;

    if(i + 2 < s.size() && (byte)s[i + 1] == 255)
    {
        value += 254 * (long)(byte)s[i + 2];
        value &= 0xFFFF;
        i += 2;
    }

    return true;
}

// Escapes a literal for the ascii encoding: \ { and } get a backslash.
void put_literal(std::string &out, const std::string &s, size_t from, size_t to)
{
    for(size_t i = from; i < to; ++i)
    {
        char ch = s[i];

        if(ch == '\\' || ch == '{' || ch == '}')
            out += '\\';

        out += ch;
    }
}

// A piece of parsed text: a literal, a byte that isn't a command, or a command.
enum { LITERAL, INVALID, COMMAND };

struct Seg
{
    int type;
    size_t from, to;
    int code, nargs;
    long args[10];
};

// The legacy text as 3.0's ascii encoding (parse_legacy_binary_msg_str, then
// ParsedMsgStr::serialize). Trailing spaces go, unless a command's arguments reach into them;
// a byte that isn't a known command is kept as it is.
std::string ascii_text(const std::string &s)
{
    std::vector<Seg> segs;
    size_t last = 0;

    for(size_t i = s.size(); i > 0; --i)
    {
        if(s[i - 1] != ' ')
        {
            last = i;
            break;
        }
    }

    size_t prev = 0, cur = 0;

    while(cur < last)
    {
        byte ch = (byte)s[cur];

        if(ch >= 32 && ch <= 126)
        {
            ++cur;
            continue;
        }

        if(prev != cur)
        {
            Seg g = { LITERAL, prev, cur, 0, 0 };
            segs.push_back(g);
            prev = cur;
        }

        const Command *cmd = find_command((int)ch - 1);

        if(!cmd)
        {
            ++cur;
            Seg g = { INVALID, prev, cur, 0, 0 };
            segs.push_back(g);
            prev = cur;
            continue;
        }

        Seg g = { COMMAND, cur, cur, cmd->code, 0 };

        while(g.nargs < cmd->args && legacy_argument(s, cur, g.args[g.nargs]))
            ++g.nargs;

        segs.push_back(g);
        ++cur;
        prev = cur;
    }

    if(prev < last)
    {
        Seg g = { LITERAL, prev, last, 0, 0 };
        segs.push_back(g);
    }

    std::string out;

    for(size_t k = 0; k < segs.size(); ++k)
    {
        const Seg &g = segs[k];

        if(g.type == COMMAND)
        {
            out += '\\';
            out += find_command(g.code)->name;

            for(int a = 0; a < g.nargs; ++a)
            {
                char num[16];
                sprintf(num, "\\%ld", g.args[a]);
                out += num;
            }

            out += k + 1 == segs.size() ? "\\" : "\\ ";
        }
        else if(g.type == LITERAL)
            put_literal(out, s, g.from, g.to);
        else
            out.append(s, g.from, g.to - g.from);
    }

    return out;
}

// ---- the section ------------------------------------------------------------------------

std::string text_of(const MsgStr &m)
{
    size_t n = 0;

    while(n < MSGSIZE + 1 && m.s[n])
        ++n;

    return std::string(m.s, n);
}

// The "more" icon the init data gives every string (3.0): sprite, anchor, x, y.
void put_more_icon(Buf &o)
{
    o.w(43);                                            // iwMore
    o.c(zinit.msg_more_is_offset ? 10 : 9);             // screen_y_offset, screen
    o.w(zinit.msg_more_x);
    o.w(zinit.msg_more_y < 160 ? zinit.msg_more_y : 160);
}

void put_string(Buf &o, const MsgStr &m, int target)
{
    bool v30 = target == ZQ_NEWER_30;
    std::string text = text_of(m);

    if(v30)
        text = ascii_text(text);

    if(text.size() > 8192)
        text.resize(8192);

    o.l((long)text.size());

    if(!text.empty())
        o.put(text.data(), text.size());

    o.w(m.nextstring);
    o.l(m.tile);
    o.c(m.cset);
    o.c(m.trans ? 1 : 0);
    o.c(m.font);
    o.w(m.x);
    o.w(m.y);
    o.w(m.w);
    o.w(m.h);
    o.c(m.hspace);
    o.c(m.vspace);
    // 3.0 clears two flags older versions defined but never used
    o.c(v30 ? (m.stringflags & ~0x0C) : m.stringflags);
    o.c(8);                     // margins: up, down, left, right
    o.c(0);
    o.c(8);
    o.c(8);
    o.l(0);                     // portrait: tile, cset, x, y, width, height
    o.c(0);
    o.c(0);
    o.c(0);
    o.c(1);
    o.c(1);
    o.c(0);                     // shadow type and colour
    o.c(0);
    o.c(6);                     // layer

    if(!v30)
    {
        o.c(m.sfx);
        o.w(m.listpos);
        return;
    }

    o.w(m.sfx);
    o.w(m.listpos);
    put_more_icon(o);

    for(int k = 0; k < 2; ++k)  // scroll up and down icons: none
    {
        o.w(0xFFFF);
        o.c(7);                 // r_down
        o.w(0);
        o.w(0);
    }

    o.c(1);                     // active and passive scroll speeds
    o.c(1);
    o.w(0);                     // advance sound, menu cursor sound, menu close sound
    o.w(m.sfx);
    o.w(0);
}

int zqn_strings(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;

    zqn_edit_test();
    Buf o;
    o.w(msg_count);

    for(int i = 0; i < msg_count; ++i)
        put_string(o, MsgStrings[i], ctx.target);

    word version = ctx.target == ZQ_NEWER_255 ? 10 : 13;
    word cversion = ctx.target == ZQ_NEWER_255 ? 2 : version;

    if(!p_mputl(ID_STRINGS, f) || !p_iputw(version, f) || !p_iputw(cversion, f) || !p_iputl((long)o.b.size(), f))
        return 1;

    if(!o.b.empty() && !pfwrite(&o.b[0], (long)o.b.size(), f))
        return 1;

    return ZQW_OK;
}

ZqNewerRegistration g_reg(ID_STRINGS, zqn_strings, "strings for 2.55.17 and 3.0");

} // namespace

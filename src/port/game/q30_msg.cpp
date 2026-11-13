// The message engine of 2.55 and 3.0 quests: a port of upstream ZQuest Classic 3.0's
// src/zc/message_string.cpp with the parts of core/msgstr.cpp (the string iterator),
// components/scc/scc.cpp (string control codes) and core/msg_scroll.cpp (scrolling text)
// it uses. 2.53 quests never come here: guys.cpp putmsg and zelda.cpp donewmsg keep 2.53's
// engine for them (q30_msg_engine() is false).
//
// What 2.53's code shares with the rest of the game stays in the same globals: msgstr is
// upstream's active_str, msg_active its active, msg_onscreen its on_screen, msgclk its clk,
// linkedmsgclk its linked_clk, msgfont its msg_font and q30_enqueued_str its enqueued_str.
// The string's frame, portrait and text (with the menu cursor) live in upstream's own
// buffers and are put together in msgdisplaybuf, which the game draws where it always has.
//
// Each string's data comes from the quest reader (q255_basic.cpp, q30_msg_set_string), as
// upstream keeps it: the text as the file has it, ascii (STR v11+) or binary encoded.

#include "precompiled.h" //always first

#include <map>
#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "zelda.h"
#include "zsys.h"
#include "zc_sys.h"
#include "guys.h"
#include "link.h"
#include "maps.h"
#include "subscr.h"
#include "tiles.h"
#include "weapons.h"
#include "items.h"
#include "gamedata.h"
#include "q30.h"
#include "q30_triggers.h"
#include "q30_audio.h"

extern LinkClass Link;
extern sprite_list items;
extern word quest_header_zelda_version;
extern dword q255_compat_rule_version;
extern int q255_dmaps_version;      // q255_maps.cpp
extern int q255_header_version[3];
extern int q30_enqueued_str;                // guys.cpp: upstream's enqueued_str
extern int lens_hint_weapon[MAXWPNS][5];
extern byte q255_msg_advance_delay;         // q255_init.cpp: zinit.msg_advance_delay
extern int q255_initdata_version;           // q255_init.cpp: the INIT section's version
bool q30_active();
bool q30_quest_rule(int id);
FONT *get_zc_font(int index);               // script_drawing.cpp
void q30_trig_copycat(byte copyid);         // q30_triggers.cpp
bool q30_msg_script_warp(int dmap, int scr, int x, int y, int effect, int sound);   // q30_scripts_cmds.h
bool q30_msg_run_generic_frozen(int script, const long *initd, int count);          // q30_scripts_cmds.h

namespace
{

// ---- upstream's rules (core/qrs.h) and the old quests readrules turns them on for ---------

enum
{
    QR_ALLOWFASTMSG = 41, QR_ALLOWMSGBYPASS = 89, QR_MSGFREEZE = 111, QR_MSGDISAPPEAR = 122,
    QR_NOITEMOFFSET = 131, QR_ITEMPICKUPSETSBELOW = 154, QR_OLD_STRING_EDITOR_MARGINS = 202,
    QR_STRING_FRAME_OLD_WIDTH_HEIGHT = 205, QR_SCC_GOTO_RESPECTS_CONTFLAG = 265, QR_OLD_HALF_MAGIC = 269,
    QR_LAYER6_STRINGS_OVER_SUBSCREEN = 447, QR_NO_FLASHING_MSG_MORE = 592, QR_BROKEN_SCC_MAP_ARGS = 600,
    QR_OLD_LONG_SCC_ARGS = 601, QR_STRINGS_DONT_SCROLL = 602, QR_STRING_SEGMENTED_SCROLL = 603
};

// What the reader decided about the string rules (it knows the build and section versions).
bool g_old_frame = false, g_old_margins = false;

bool header_before(int major, int minor, int patch)
{
    const int *v = q255_header_version;
    return v[0] < major || (v[0] == major && (v[1] < minor || (v[1] == minor && v[2] < patch)));
}

// get_qr() for this engine: the quest's bit, or upstream's readrules turning the rule on for
// quests saved before it existed (core/qst_rules.cpp)
bool qr(int id)
{
    switch(id)
    {
    case QR_OLD_STRING_EDITOR_MARGINS:
        return g_old_margins || q30_quest_rule(id);
    case QR_STRING_FRAME_OLD_WIDTH_HEIGHT:
        return g_old_frame || q30_quest_rule(id);
    case QR_OLD_HALF_MAGIC:
        return q30_quest_rule(id) || q255_compat_rule_version < 23;
    case QR_NO_FLASHING_MSG_MORE:
        return q30_quest_rule(id) || q255_compat_rule_version < 114;
    case QR_LAYER6_STRINGS_OVER_SUBSCREEN:
        // 2.55 quests before 2.55.10 (qst_rules.cpp), and z3 quests before compat 77 (qst.cpp)
        if(q255_header_version[0] >= 3)
            return q30_quest_rule(id) || (q255_compat_rule_version < 77 && q255_dmaps_version >= 22);
        return q30_quest_rule(id) || header_before(2, 55, 10);
    case QR_OLD_LONG_SCC_ARGS:
        return q30_quest_rule(id) || q255_compat_rule_version < 119;
    case QR_STRINGS_DONT_SCROLL:
        return q30_quest_rule(id) || q255_compat_rule_version < 120;
    case QR_BROKEN_SCC_MAP_ARGS:
        // 2.55's branch can't raise the compat version, so it goes by the header's version
        if(q255_header_version[0] >= 3)
            return q30_quest_rule(id) || q255_compat_rule_version < 118;
        return q30_quest_rule(id) || header_before(2, 55, 15);
    }

    return q30_quest_rule(id);
}

// ---- the strings, as the reader gives them ---------------------------------------------------

std::vector<Q30MsgStr> g_strings;

// ---- string control codes (components/scc/scc.h) ---------------------------------------------

enum
{
    QMSGC_COLOUR = 1, QMSGC_SPEED = 2, QMSGC_GOTOIFSCREEND = 3, QMSGC_GOTOIFRAND = 4, QMSGC_GOTOIFITEM = 5,
    QMSGC_GOTOIFCTR = 6, QMSGC_GOTOIFCTRPC = 7, QMSGC_GOTOIFTRI = 8, QMSGC_GOTOIFTRICOUNT = 9, QMSGC_CTRUP = 10,
    QMSGC_CTRDN = 11, QMSGC_CTRSET = 12, QMSGC_CTRUPPC = 13, QMSGC_CTRDNPC = 14, QMSGC_CTRSETPC = 15,
    QMSGC_GIVEITEM = 16, QMSGC_TAKEITEM = 17, QMSGC_WARP = 18, QMSGC_SETSCREEND = 19, QMSGC_SFX = 20,
    QMSGC_MIDI = 21, QMSGC_NAME = 22, QMSGC_GOTOIFCREEND = 23, QMSGC_CHANGEPORTRAIT = 24, QMSGC_NEWLINE = 25,
    QMSGC_SHDCOLOR = 26, QMSGC_SHDTYPE = 27, QMSGC_DRAWTILE = 28, QMSGC_ENDSTRING = 29, QMSGC_WAIT_ADVANCE = 30,
    QMSGC_SETUPMENU = 128, QMSGC_MENUCHOICE = 129, QMSGC_RUNMENU = 130, QMSGC_GOTOMENUCHOICE = 131,
    QMSGC_TRIGSECRETS = 132, QMSGC_SETSCREENSTATE = 133, QMSGC_SETSCREENSTATER = 134, QMSGC_FONT = 135,
    QMSGC_RUN_FRZ_GENSCR = 136, QMSGC_TRIG_CMB_COPYCAT = 137, QMSGC_GOTOIFGLOBALSTATE = 138,
    QMSGC_GOTOIFSCREENSTATE = 139, QMSGC_GOTOIFANYSCREENSTATE = 140, QMSGC_GOTOIFSCREENEXSTATE = 141,
    QMSGC_GOTOIFANYSCREENEXSTATE = 142, QMSGC_GOTOIFLEVELSTATE = 143, QMSGC_GOTOIFLEVELITEM = 144,
    QMSGC_GOTOIFSECRETS = 145, QMSGC_SETSCREENEXSTATE = 146, QMSGC_SETANYSCREENEXSTATE = 147,
    QMSGC_SETGLOBALSTATE = 148, QMSGC_SETLEVELSTATE = 149, QMSGC_SETLEVELITEM = 150, QMSGC_COUNTER = 151,
    QMSGC_MAXCOUNTER = 152, QMSGC_KILLHERO = 153, QMSGC_COLLECTITEM = 154, QMSGC_DELAY = 155,
    QMSGC_FORCE_DELAY = 156, QMSGC_GOTOIFBOTTLE = 157, QMSGC_CHANGEBOTTLE = 158, QMSGC_MUSIC = 159,
    QMSGC_MUSIC_REFRESH = 160, QMSGC_SETCURRENTSCREEND = 161
};

enum { MAX_SCC_ARG_COUNT = 10, MAX_SCC_INT = 214748, MIN_SCC_INT = -214748 };

// upstream's mapscr.h: the screen states (game->maps keeps 16 of them) and mSPECIALITEM (mBELOW)
enum { mMAXIND = 17, mSPECIALITEM = 32 };
// The screen states this engine can store: game->maps entries are 16-bit words, so upstream's
// state 16 (mNO_ENEMIES_RETURN, 0x10000) has no bit here and setting it is refused.
enum { Q30_MSG_STORED_STATES = 16 };

struct SccDefn
{
    const char *name;
    int code, min_args, max_args;
};

// scc.cpp's scc_commands
const SccDefn scc_commands[] =
{
    { "TextColor", QMSGC_COLOUR, 2, 2 }, { "ShadowColor", QMSGC_SHDCOLOR, 2, 2 }, { "ShadowType", QMSGC_SHDTYPE, 1, 1 },
    { "Speed", QMSGC_SPEED, 1, 1 }, { "Tile", QMSGC_DRAWTILE, 5, 5 }, { "Newline", QMSGC_NEWLINE, 0, 0 },
    { "Font", QMSGC_FONT, 1, 1 }, { "Name", QMSGC_NAME, 0, 0 }, { "Portrait", QMSGC_CHANGEPORTRAIT, 6, 6 },
    { "SetupMenu", QMSGC_SETUPMENU, 5, 5 }, { "MenuChoice", QMSGC_MENUCHOICE, 5, 5 }, { "RunMenu", QMSGC_RUNMENU, 0, 0 },
    { "GoIfMenuChoice", QMSGC_GOTOMENUCHOICE, 2, 2 }, { "GoIfScreenD", QMSGC_GOTOIFSCREEND, 3, 3 },
    { "GoIfRand", QMSGC_GOTOIFRAND, 2, 2 }, { "GoIfItem", QMSGC_GOTOIFITEM, 2, 2 }, { "GoIfCounter", QMSGC_GOTOIFCTR, 3, 3 },
    { "GoIfCounter%", QMSGC_GOTOIFCTRPC, 3, 3 }, { "GoIfMcGuffin", QMSGC_GOTOIFTRI, 2, 2 },
    { "GoIfMcGuffinCount", QMSGC_GOTOIFTRICOUNT, 2, 2 }, { "GoIfAnyScreenD", QMSGC_GOTOIFCREEND, 5, 5 },
    { "GoIfGlobalState", QMSGC_GOTOIFGLOBALSTATE, 3, 3 }, { "GoIfScreenState", QMSGC_GOTOIFSCREENSTATE, 3, 3 },
    { "GoIfAnyScreenState", QMSGC_GOTOIFANYSCREENSTATE, 5, 5 }, { "GoIfScreenExState", QMSGC_GOTOIFSCREENEXSTATE, 3, 3 },
    { "GoIfAnyScreenExState", QMSGC_GOTOIFANYSCREENEXSTATE, 5, 5 }, { "GoIfLevelState", QMSGC_GOTOIFLEVELSTATE, 4, 4 },
    { "GoIfLevelItem", QMSGC_GOTOIFLEVELITEM, 4, 4 }, { "GoIfSecrets", QMSGC_GOTOIFSECRETS, 1, 1 },
    { "GoIfBottle", QMSGC_GOTOIFBOTTLE, 3, 3 }, { "CounterAdd", QMSGC_CTRUP, 2, 2 }, { "CounterSubtract", QMSGC_CTRDN, 2, 2 },
    { "CounterSet", QMSGC_CTRSET, 2, 2 }, { "CounterAdd%", QMSGC_CTRUPPC, 2, 2 }, { "CounterSubtract%", QMSGC_CTRDNPC, 2, 2 },
    { "CounterSet%", QMSGC_CTRSETPC, 2, 2 }, { "GiveItem", QMSGC_GIVEITEM, 1, 1 }, { "CollectItem", QMSGC_COLLECTITEM, 1, 1 },
    { "TakeItem", QMSGC_TAKEITEM, 1, 1 }, { "ChangeBottle", QMSGC_CHANGEBOTTLE, 3, 3 }, { "Warp", QMSGC_WARP, 6, 6 },
    { "SetScreenD", QMSGC_SETSCREEND, 4, 4 }, { "SetCurrentScreenD", QMSGC_SETCURRENTSCREEND, 2, 2 },
    { "SFX", QMSGC_SFX, 1, 1 }, { "MIDI", QMSGC_MIDI, 1, 1 }, { "Music", QMSGC_MUSIC, 1, 1 },
    { "MusicRefresh", QMSGC_MUSIC_REFRESH, 1, 1 }, { "WaitAdvance", QMSGC_WAIT_ADVANCE, 0, 0 },
    { "Delay", QMSGC_DELAY, 1, 1 }, { "ForceDelay", QMSGC_FORCE_DELAY, 1, 1 }, { "EndString", QMSGC_ENDSTRING, 0, 0 },
    { "TriggerSecrets", QMSGC_TRIGSECRETS, 1, 1 }, { "TriggerComboCopycat", QMSGC_TRIG_CMB_COPYCAT, 1, 1 },
    { "SetScreenState", QMSGC_SETSCREENSTATE, 2, 2 }, { "SetAnyScreenState", QMSGC_SETSCREENSTATER, 4, 4 },
    { "SetScreenExState", QMSGC_SETSCREENEXSTATE, 2, 2 }, { "SetAnyScreenExState", QMSGC_SETANYSCREENEXSTATE, 4, 4 },
    { "SetGlobalState", QMSGC_SETGLOBALSTATE, 2, 2 }, { "SetLevelState", QMSGC_SETLEVELSTATE, 3, 3 },
    { "SetLevelItem", QMSGC_SETLEVELITEM, 3, 3 }, { "RunFrozenGenericScript", QMSGC_RUN_FRZ_GENSCR, 2, 10 },
    { "Counter", QMSGC_COUNTER, 1, 1 }, { "MaxCounter", QMSGC_MAXCOUNTER, 1, 1 }, { "KillHero", QMSGC_KILLHERO, 1, 1 },
};

const SccDefn *find_scc(int code)
{
    for(size_t i = 0; i < sizeof(scc_commands) / sizeof(scc_commands[0]); ++i)
        if(scc_commands[i].code == code)
            return &scc_commands[i];
    return NULL;
}

// A command and its arguments as upstream's zfix: the value times 10000.
struct Command
{
    int code;
    int num_args;
    long args[MAX_SCC_ARG_COUNT];

    Command() : code(0), num_args(0) { memset(args, 0, sizeof(args)); }

    // zfix's int conversion (getInt): rounds half away from zero
    int arg(int i) const
    {
        const long v = args[i];
        long r = (v % 10000) / 5000;
        if(v < 0 && r > 0) r = 0;
        if(v >= 0 && r < 0) r = 0;
        return (int)(v / 10000 + r);
    }
    long zlong(int i) const { return args[i]; }        // getZLong
    int trunc(int i) const { return (int)(args[i] / 10000); }
};

// A parsed string (scc.h's ParsedMsgStr): literal and command segments in order.
struct Segment
{
    bool command;
    std::string text;       // a literal's text
    Command cmd;
};

typedef std::vector<Segment> Parsed;

// parse_ascii_number: false if there is no number at start
struct Number
{
    long value;             // times 10000
    size_t next;
    bool has_decimal;
    int errs;
};

enum { NUM_OVERFLOW = 1, NUM_UNDERFLOW = 2, NUM_TOO_MANY_DEC = 4, NUM_TOO_FEW_DEC = 8 };

bool parse_number(const std::string &str, size_t start, Number &out)
{
    size_t i = start;

    if(i >= str.size())
        return false;

    bool negative = false;

    if(str[i] == '-')
    {
        negative = true;
        ++i;
    }

    if(i >= str.size() || !isdigit((unsigned char)str[i]))
        return false;

    int ipart = 0, dpart = 0, errs = 0;
    bool has_decimal = false;

    while(i < str.size() && isdigit((unsigned char)str[i]))
    {
        const int digit = str[i] - '0';

        if(!errs)
        {
            if(negative)
            {
                if(ipart < MIN_SCC_INT / 10 || (ipart == MIN_SCC_INT / 10 && -digit < MIN_SCC_INT % 10))
                {
                    errs |= NUM_UNDERFLOW;
                    ipart = MIN_SCC_INT;
                }
                else
                    ipart = ipart * 10 - digit;
            }
            else
            {
                if(ipart > MAX_SCC_INT / 10 || (ipart == MAX_SCC_INT / 10 && digit > MAX_SCC_INT % 10))
                {
                    errs |= NUM_OVERFLOW;
                    ipart = MAX_SCC_INT;
                }
                else
                    ipart = ipart * 10 + digit;
            }
        }

        ++i;
    }

    if(i < str.size() && str[i] == '.')
    {
        has_decimal = true;
        ++i;
        int place = 1000, max_dpart = 9999;

        if(ipart == MAX_SCC_INT)
            max_dpart = 3647;
        else if(ipart == MIN_SCC_INT)
            max_dpart = 3648;

        if(errs & (NUM_OVERFLOW | NUM_UNDERFLOW))
            dpart = max_dpart;

        if(i >= str.size() || !isdigit((unsigned char)str[i]))
            errs |= NUM_TOO_FEW_DEC;
        else
        {
            while(i < str.size() && place && isdigit((unsigned char)str[i]))
            {
                if(!errs)
                {
                    dpart += place * (str[i] - '0');

                    if(dpart > max_dpart)
                    {
                        errs |= negative ? NUM_UNDERFLOW : NUM_OVERFLOW;
                        dpart = max_dpart;
                    }
                }

                ++i;
                place /= 10;
            }

            if(i < str.size() && !place && isdigit((unsigned char)str[i]))
            {
                errs |= NUM_TOO_MANY_DEC;

                while(i < str.size() && isdigit((unsigned char)str[i]))
                    ++i;
            }
        }
    }

    out.value = (long)ipart * 10000 + (negative ? -dpart : dpart);
    out.next = i;
    out.has_decimal = has_decimal;
    out.errs = errs;
    return true;
}

void skip_to_slash(const std::string &str, size_t &cur)
{
    while(cur < str.size() && str[cur] != '\\')
        ++cur;
}

bool arg_allows_decimal(int code, int arg)
{
    switch(code)
    {
    case QMSGC_GOTOIFSCREEND:
    case QMSGC_SETCURRENTSCREEND:
        return arg == 1;
    case QMSGC_SETSCREEND:
    case QMSGC_GOTOIFCREEND:
        return arg == 3;
    case QMSGC_RUN_FRZ_GENSCR:
        return arg >= 2 && arg < 10;
    }

    return false;
}

// parse_ascii_scc_command, with str[cur] on the opening backslash
bool parse_ascii_command(const std::string &str, size_t &cur, Command &cmd)
{
    const size_t last = str.size();
    ++cur;

    bool has_code = false, decimal_code = false;
    long code = 0;

    if(cur < last && isalpha((unsigned char)str[cur]))
    {
        const size_t start = cur;
        skip_to_slash(str, cur);
        const std::string name = str.substr(start, cur - start);

        for(size_t i = 0; i < sizeof(scc_commands) / sizeof(scc_commands[0]); ++i)
        {
            const char *known = scc_commands[i].name;
            size_t j = 0;

            if(strlen(known) != name.size())
                continue;

            while(j < name.size() && tolower((unsigned char)known[j]) == tolower((unsigned char)name[j]))
                ++j;

            if(j == name.size())
            {
                code = scc_commands[i].code;
                has_code = true;
                break;
            }
        }
    }
    else
    {
        Number n;

        if(parse_number(str, cur, n))
        {
            decimal_code = n.has_decimal;
            cur = n.next;
            code = n.value / 10000;
            has_code = true;
        }
    }

    if(!has_code)
        skip_to_slash(str, cur);

    // upstream's StringCommand keeps the code in a byte, and looks that up
    const int code_byte = (int)(byte)code;
    const SccDefn *defn = find_scc(code_byte);
    cmd.code = code_byte;
    const int min_args = defn ? defn->min_args : 0, max_args = defn ? defn->max_args : 0;

    int found = 0, arg_errors = 0;
    bool non_numeric = false, double_slash = false, bad_decimal = false;

    while(cur < last)
    {
        if(str[cur] != '\\')
            break;

        if(cur + 1 == last || str[cur + 1] == ' ')
            break;

        ++cur;

        if(str[cur] == '\\')
        {
            double_slash = true;
            --cur;
            break;
        }

        long value = 0;
        Number n;

        if(!parse_number(str, cur, n))
        {
            non_numeric = true;
            skip_to_slash(str, cur);
        }
        else if(n.next < last && str[n.next] == '.')
        {
            non_numeric = true;
            cur = n.next;
            skip_to_slash(str, cur);
        }
        else
        {
            if(n.has_decimal && !arg_allows_decimal(cmd.code, found))
            {
                bad_decimal = true;
                n.value = (n.value / 10000) * 10000;    // doTrunc
                n.errs &= ~(NUM_TOO_FEW_DEC | NUM_TOO_MANY_DEC);
            }

            arg_errors |= n.errs;
            value = n.value;
            cur = n.next;
        }

        if(found < MAX_SCC_ARG_COUNT)
            cmd.args[found] = value;

        ++found;
    }

    cmd.num_args = found < MAX_SCC_ARG_COUNT ? found : MAX_SCC_ARG_COUNT;

    const bool missing_slash = cur == last || str[cur] != '\\';

    if(!missing_slash)
        ++cur;

    bool valid = cmd.code != 0 && !missing_slash && defn != NULL && found >= min_args && found <= max_args
                 && !arg_errors && !non_numeric && !double_slash && !bad_decimal && !decimal_code;

    if(cur == last)
        return valid;

    if(str[cur] == ' ')
        ++cur;
    else
        valid = false;

    return valid;
}

void push_literal(Parsed &out, const std::string &str, size_t &start, size_t end)
{
    if(start != std::string::npos && end > start)
    {
        Segment s;
        s.command = false;
        s.text = str.substr(start, end - start);
        out.push_back(s);
    }

    start = std::string::npos;
}

void push_command(Parsed &out, const Command &cmd)
{
    Segment s;
    s.command = true;
    s.cmd = cmd;
    out.push_back(s);
}

// parse_ascii_msg_str (an invalid segment stays text, as upstream draws it)
Parsed parse_ascii(const std::string &str)
{
    Parsed out;
    const size_t none = std::string::npos, last = str.size();
    size_t cur = 0, literal_start = 0;

    while(cur < last)
    {
        if(literal_start == none)
            literal_start = cur;

        const char c = str[cur];

        if(c == '\n')
        {
            push_literal(out, str, literal_start, cur++);
            Command nl;
            nl.code = QMSGC_NEWLINE;
            push_command(out, nl);
            continue;
        }

        if(c == '{')
        {
            push_literal(out, str, literal_start, cur);
            literal_start = cur;
            ++cur;

            while(cur < last)
            {
                if(str[cur] == '}' && str[cur - 1] != '\\')
                {
                    ++cur;
                    break;
                }

                ++cur;
            }

            push_literal(out, str, literal_start, cur++);
            continue;
        }

        if(c != '\\')
        {
            ++cur;
            continue;
        }

        if(cur + 1 < last)
        {
            const char next = str[cur + 1];

            if(next == '{' || next == '}')
            {
                push_literal(out, str, literal_start, cur);
                literal_start = cur + 1;
                cur += 2;
                continue;
            }

            if(next == '\\')
            {
                push_literal(out, str, literal_start, cur + 1);
                cur += 2;
                continue;
            }
        }

        if(cur + 1 == last)
        {
            push_literal(out, str, literal_start, cur);
            literal_start = cur;
            ++cur;
            push_literal(out, str, literal_start, cur);
            break;
        }

        const size_t command_start = cur;
        Command cmd;

        if(parse_ascii_command(str, cur, cmd))
        {
            push_literal(out, str, literal_start, command_start);
            push_command(out, cmd);
        }
        else
            push_literal(out, str, literal_start, cur);
    }

    push_literal(out, str, literal_start, last);
    return out;
}

// parse_legacy_binary_msg_str
Parsed parse_binary(const std::string &str)
{
    Parsed out;
    size_t last = 0;

    for(size_t i = str.size(); i > 0; --i)
    {
        if(str[i - 1] != ' ')
        {
            last = i;
            break;
        }
    }

    size_t prev = 0, cur = 0;

    while(cur < last)
    {
        const byte c = (byte)str[cur];

        if(c >= 32 && c <= 126)
        {
            ++cur;
            continue;
        }

        if(prev != cur)
        {
            Segment s;
            s.command = false;
            s.text = str.substr(prev, cur - prev);
            out.push_back(s);
            prev = cur;
        }

        const SccDefn *defn = find_scc((int)c - 1);

        if(!defn)
        {
            // an unknown command byte stays text (an invalid segment)
            ++cur;
            Segment s;
            s.command = false;
            s.text = str.substr(prev, cur - prev);
            out.push_back(s);
            prev = cur;
            continue;
        }

        Command cmd;
        cmd.code = (int)c - 1;
        int found = 0;

        for(; found < defn->min_args; ++found)
        {
            // parse_legacy_binary_argument
            if(cur + 1 >= str.size())
                break;

            byte val = (byte)str[++cur] - 1;
            long ret = val;

            if(cur + 2 < str.size() && (byte)str[cur + 1] == 255)
            {
                ret += 254 * (long)(byte)str[cur + 2];
                cur += 2;
            }

            cmd.args[found] = ret * 10000;
        }

        cmd.num_args = found;
        push_command(out, cmd);
        ++cur;
        prev = cur;
    }

    if(prev < last)
    {
        Segment s;
        s.command = false;
        s.text = str.substr(prev, last - prev);
        out.push_back(s);
    }

    return out;
}

std::map<int, Parsed> g_parsed;     // by string, made when first shown

const Q30MsgStr &str_data(int str)
{
    static Q30MsgStr none;
    return (str >= 0 && (size_t)str < g_strings.size()) ? g_strings[str] : none;
}

const Parsed &parsed(int str)
{
    std::map<int, Parsed>::iterator it = g_parsed.find(str);

    if(it != g_parsed.end())
        return it->second;

    const Q30MsgStr &s = str_data(str);
    return g_parsed[str] = s.ascii ? parse_ascii(s.text) : parse_binary(s.text);
}

// ---- MsgStr::iterator (core/msgstr.cpp) -------------------------------------------------------

struct Iterator
{
    enum State { NOT_STARTED, CHARACTER, COMMAND, IDLE, DONE };

    int str;                // the string whose segments it walks
    State state;
    std::string character;  // the character (state CHARACTER)
    Command command;        // the command (state COMMAND)

    std::string buffer, word;
    size_t segment_index;
    int j, k;
    int post_segment_delay;
    bool post_segment_delay_forced, post_segment_delay_fast;

    explicit Iterator(int s) : str(s), state(NOT_STARTED), segment_index(0), j(0), k(0),
        post_segment_delay(0), post_segment_delay_forced(false), post_segment_delay_fast(false) {}

    bool next() { return next_character(); }

    void set_buffer(const std::string &text)
    {
        buffer = text;
        state = buffer.empty() ? IDLE : CHARACTER;
        word = "";
        j = 0;
        k = 0;
    }

    bool done() const { return state == DONE; }

    std::string peek(int n) const
    {
        if(k + n < (int)word.length())
            return word.substr(k + n, 1);

        const long i = (long)j + n - ((long)word.length() - k + 1);

        if(i >= 0 && i < (long)buffer.length())
            return buffer.substr((size_t)i, 1);

        return "";
    }

    const char *remaining_word() const
    {
        if(k < 1)
            return word.c_str();
        return word.c_str() + k - 1;
    }

    bool next_segment()
    {
        buffer = "";
        j = 0;
        k = 0;

        const Parsed &p = parsed(str);

        if(segment_index >= p.size())
        {
            state = DONE;
            return true;
        }

        const Segment &s = p[segment_index++];

        if(s.command)
        {
            state = COMMAND;
            command = s.cmd;
        }
        else
        {
            state = CHARACTER;
            buffer = s.text;
        }

        return false;
    }

    bool next_word()
    {
        k = 0;
        word = "";

        if(j >= (int)buffer.size())
        {
            if(post_segment_delay > 0)
            {
                state = IDLE;
                if(!post_segment_delay_forced && post_segment_delay_fast)
                    post_segment_delay = zc_max(0, post_segment_delay - 5);
                else
                    post_segment_delay--;
                return false;
            }

            if(next_segment())
                return true;
        }

        if(buffer.empty())
            return false;

        if(str_data(str).stringflags & STRINGFLAG_WRAP)
        {
            if(buffer[j] == ' ')        // spaces are one-character words
                word = buffer[j++];
            else
            {
                const int word_start = j;
                while(j < (int)buffer.size() && buffer[j] != ' ')
                    j++;
                word = buffer.substr(word_start, j - word_start);
            }
        }
        else
            word = buffer[j++];

        return false;
    }

    bool next_character()
    {
        character = "";

        if(state == NOT_STARTED || k == (int)word.size())
            if(next_word())
                return true;

        if(state == COMMAND)
            return false;

        if(k < (int)word.size())
        {
            state = CHARACTER;
            character = word[k++];
            return false;
        }

        state = IDLE;
        return false;
    }

    void set_post_segment_delay(int frames, bool forced)
    {
        post_segment_delay = frames;
        post_segment_delay_forced = forced;
        post_segment_delay_fast = false;
    }
};

// ---- MsgScrollState and msg_layout (core/msg_scroll.cpp) ---------------------------------------

struct Scroll
{
    bool can_scroll, segmented, active_scrolling;
    int scroll_pos, target_scroll_pos, max_visible_pos, body_height, active_speed, passive_speed;

    enum { segment_gap = 2 };

    Scroll() { clear(); }

    int segment_stride() const { return body_height + segment_gap; }

    void clear()
    {
        can_scroll = segmented = active_scrolling = false;
        scroll_pos = target_scroll_pos = max_visible_pos = 0;
        body_height = 1;
        active_speed = passive_speed = 1;
    }

    void set_body_height(int height, int up, int down) { body_height = zc_max(1, height - up - down); }
    int cur_scroll() const { return can_scroll ? scroll_pos : 0; }

    int max_scroll() const
    {
        if(!can_scroll || max_visible_pos <= body_height)
            return 0;
        return zc_max(0, max_visible_pos - body_height);
    }

    int bound(int v) const { return vbound(v, 0, max_scroll()); }
    bool at_max() const { return cur_scroll() == max_scroll(); }

    bool update_max_scroll(int pos)
    {
        if(segmented && pos > 0)
        {
            const int seg = (pos - 1) / segment_stride();
            pos = seg * segment_stride() + body_height;
        }

        if(pos <= max_visible_pos)
            return false;
        max_visible_pos = pos;
        return true;
    }

    bool ensure_scrolled_to(int pos, int h)
    {
        const int epos = pos + h;
        const bool grew = update_max_scroll(epos);

        if(target_scroll_pos + body_height < epos)
            target_scroll_pos = segmented ? pos - (pos % segment_stride()) : epos - body_height;
        else if(target_scroll_pos > pos)
            target_scroll_pos = segmented ? pos - (pos % segment_stride()) : pos;

        return grew;
    }

    bool tick()
    {
        if(!can_scroll || scroll_pos == target_scroll_pos)
            return false;

        const int spd = active_scrolling ? active_speed : passive_speed;

        if(!spd)
            scroll_pos = target_scroll_pos;
        else if(scroll_pos < target_scroll_pos)     // util::move_towards
            scroll_pos = zc_min(scroll_pos + spd, target_scroll_pos);
        else
            scroll_pos = zc_max(scroll_pos - spd, target_scroll_pos);

        if(scroll_pos == target_scroll_pos)
            active_scrolling = false;
        return true;
    }

    void scroll_input(bool up, bool down)
    {
        if(up == down)
            return;
        if(down ? scroll_pos >= max_scroll() : scroll_pos <= 0)
            return;

        if(segmented)
        {
            active_scrolling = true;
            target_scroll_pos = bound(target_scroll_pos + (down ? segment_stride() : -segment_stride()));
        }
        else
        {
            scroll_pos = bound(scroll_pos + (down ? active_speed : -active_speed));
            target_scroll_pos = scroll_pos;
        }
    }

    // a line advance from content row ty to ty2 that crossed into a new segment: the row the
    // cursor restarts at (segmented mode only)
    bool segment_crossed(int ty, int ty2, int *row)
    {
        if(!segmented)
            return false;

        const int seg = ty / segment_stride();

        if(ty2 <= seg * segment_stride() + body_height)
            return false;

        max_visible_pos = seg * segment_stride() + body_height;
        *row = (seg + 1) * segment_stride();
        return true;
    }
};

bool layout_bottom_margin_clip(int msg_cur_y, int height, int margin_down, bool can_scroll, bool old_margins_qr, bool old_frame_qr)
{
    return !can_scroll && !old_margins_qr && msg_cur_y >= (height + (old_frame_qr ? 16 : 0) - margin_down);
}

bool layout_wrap_needed(int msg_cur_x, int hspace, int advance_w, int width, int margin_right)
{
    return msg_cur_x + hspace + advance_w > width - margin_right;
}

bool layout_char_wrap_needed(int msg_cur_x, int tlength, int width, int margin_right, bool wrap_flag, bool rem_word_is_space)
{
    if(msg_cur_x + tlength <= width - margin_right)
        return false;
    if(msg_cur_x > width - margin_right || !wrap_flag)
        return true;
    return !rem_word_is_space;
}

// ---- message_string.cpp's file-local state -----------------------------------------------------

enum { up_m, down_m, left_m, right_m };      // margins[]: upstream's up, down, left, right

enum
{
    MNU_CURSOR_TILE, MNU_CURSOR_CSET, MNU_CURSOR_WID, MNU_CURSOR_HEI, MNU_CURSOR_FLIP,
    MNU_CHOSEN, MNU_TIMER, MNU_CAN_CONFIRM, MNU_DATA_MAX
};

struct MenuChoice
{
    int x, y, pos, upos, dpos, lpos, rpos;
};

struct Portrait
{
    long tile;
    byte cset, x, y, tw, th;

    void clear() { tile = 0; cset = 0; x = 0; y = 0; tw = 1; th = 1; }
};

int msg_menu_data[MNU_DATA_MAX];
BITMAP *txt_bmp_buf = NULL, *bg_bmp_buf = NULL, *portrait_bmp_buf = NULL, *menu_bmp_buf = NULL;
BITMAP *txt_display_buf = NULL, *bg_display_buf = NULL, *portrait_display_buf = NULL;
bool do_run_menu = false, just_confirmed_menu = false, do_end_str = false;
bool wait_advance = false, wait_advance_unprocessed_iter = false;
std::map<int, MenuChoice> menu_options;
word xpos = 0, ypos = 0, msg_cur_x = 0, msg_cur_y = 0, width = 0, height = 0;
word speed = 0;
int used_height = -1, used_height_buf = -1;
byte shadow_type = 0, shadow_color = 0, text_color = 0;
Iterator *cur_iterator = NULL;
Portrait portrait;
short margins[4] = { 0, 0, 0, 0 };
byte layer = 6;
bool nofreeze = false;
Scroll scroll;
int txt_bmp_height = 0;
const int txt_bmp_width = 512 + 16;
std::vector<long> g_collect_uids;      // CollectItem's items, picked up once Link may move

// the hint icons' animation (upstream's lens_hint_sprite, by icon type): aclk, aframe
enum { icon_more, icon_scroll_up, icon_scroll_down };
int icon_anim[3][2];
bool icon_anim_live[3];

// upstream's old_margins: what qr_OLD_STRING_EDITOR_MARGINS and dismiss() use
const short old_margins[4] = { 8, 0, 8, -8 };

const Q30MsgStr &get_str()
{
    return str_data(msgstr);
}

void reset_iterator(int str)
{
    delete cur_iterator;
    cur_iterator = str >= 0 ? new Iterator(str) : NULL;
}

bool allocate_bmps()
{
    if(txt_bmp_buf)
        return true;

    txt_bmp_height = 512 + 16;
    txt_bmp_buf = create_bitmap_ex(8, txt_bmp_width, txt_bmp_height);
    bg_bmp_buf = create_bitmap_ex(8, 512 + 16, 512 + 16);
    portrait_bmp_buf = create_bitmap_ex(8, 256, 256);
    menu_bmp_buf = create_bitmap_ex(8, txt_bmp_width, txt_bmp_height);
    txt_display_buf = create_bitmap_ex(8, 256, 224);
    bg_display_buf = create_bitmap_ex(8, 256, 224);
    portrait_display_buf = create_bitmap_ex(8, 256, 256);

    if(!txt_bmp_buf || !bg_bmp_buf || !portrait_bmp_buf || !menu_bmp_buf || !txt_display_buf || !bg_display_buf || !portrait_display_buf)
        return false;

    clear_bitmap(txt_bmp_buf);
    clear_bitmap(bg_bmp_buf);
    clear_bitmap(portrait_bmp_buf);
    clear_bitmap(menu_bmp_buf);
    clear_bitmap(txt_display_buf);
    set_clip_state(txt_display_buf, 1);
    clear_bitmap(bg_display_buf);
    set_clip_state(bg_display_buf, 1);
    clear_bitmap(portrait_display_buf);
    set_clip_state(portrait_display_buf, 1);
    return true;
}

void setmsg(int str)
{
    msgstr = str;
    reset_iterator(str);
    msgfont = get_zc_font(get_str().font);
}

// overtileblock16 (tiles.cpp upstream): a block of 16x16 tiles, flipped as a whole
void overtileblock16(BITMAP *dest, long tile, int x, int y, int w, int h, int cs, int flip)
{
    for(int j = 0; j < h; ++j)
        for(int k = 0; k < w; ++k)
        {
            const int tx = (flip & 1) ? x + ((w - 1) - k) * 16 : x + k * 16;
            const int ty = (flip & 2) ? y + ((h - 1) - j) * 16 : y + j * 16;
            overtile16(dest, (int)(tile + j * TILES_PER_ROW + k), tx, ty, cs, flip);
        }
}

int ceil16(int v) { return (v + 15) / 16; }     // (int32_t)ceil(v/16.0) for v >= 0

void draw_bg(const Q30MsgStr &msg)
{
    if(msg.tile == 0)
        return;

    if(msg.stringflags & Q30_STRINGFLAG_FULLTILE)
        draw_block_flip(bg_bmp_buf, 0, 0, (int)msg.tile, msg.cset, ceil16(msg.w), ceil16(msg.h), 0, true, false);
    else
    {
        const int add = qr(QR_STRING_FRAME_OLD_WIDTH_HEIGHT) ? 2 : 0;
        frame2x2(bg_bmp_buf, &QMisc, 0, 0, (int)msg.tile, msg.cset, (msg.w >> 3) + add, (msg.h >> 3) + add, 0, true, 0);
    }
}

void draw_prt()
{
    clear_bitmap(portrait_bmp_buf);

    if(portrait.tile > 0 && portrait.th > 0 && portrait.tw > 0)
        draw_block_flip(portrait_bmp_buf, 0, 0, (int)portrait.tile, portrait.cset, portrait.tw, portrait.th, 0, true, false);
}

void ensure_txt_bmp_height(int new_height)
{
    new_height += margins[down_m];     // the bottom margin, as extra

    if(txt_bmp_height >= new_height)
        return;

    new_height += 64;   // over-allocate a bit, so it isn't remade every line
    BITMAP *tmp_txt = txt_bmp_buf, *tmp_menu = menu_bmp_buf;
    txt_bmp_buf = create_bitmap_ex(8, txt_bmp_width, new_height);
    menu_bmp_buf = create_bitmap_ex(8, txt_bmp_width, new_height);
    clear_bitmap(txt_bmp_buf);
    clear_bitmap(menu_bmp_buf);
    blit(tmp_txt, txt_bmp_buf, 0, 0, 0, 0, txt_bmp_width, txt_bmp_height);
    blit(tmp_menu, menu_bmp_buf, 0, 0, 0, 0, txt_bmp_width, txt_bmp_height);
    destroy_bitmap(tmp_txt);
    destroy_bitmap(tmp_menu);
    txt_bmp_height = new_height;
}

void ensure_scrolled_to(int pos, int h)
{
    if(scroll.ensure_scrolled_to(pos, h))
        ensure_txt_bmp_height(scroll.max_visible_pos + margins[up_m] + margins[down_m]);
}

void update()
{
    if(!msgstr)
        return;

    set_clip_state(bg_display_buf, 0);
    blit(bg_bmp_buf, bg_display_buf, 0, 0, xpos, ypos, width + 16, height + 16);
    set_clip_state(txt_display_buf, 0);

    if(qr(QR_OLD_STRING_EDITOR_MARGINS))
    {
        blit(txt_bmp_buf, txt_display_buf, 0, 0, xpos, ypos, width + 16, height + 16);
        masked_blit(menu_bmp_buf, txt_display_buf, 0, 0, xpos, ypos, width + 16, height + 16);
    }
    else
    {
        // 1px of leeway on every side: a shadow is drawn 1px outside its glyph
        const int ty = scroll.cur_scroll();
        blit(txt_bmp_buf, txt_display_buf, margins[left_m] - 1, ty + margins[up_m] - 1, xpos + margins[left_m] - 1, ypos + margins[up_m] - 1,
             width - margins[left_m] - margins[right_m] + 2, height - margins[up_m] - margins[down_m] + 2);
        masked_blit(menu_bmp_buf, txt_display_buf, margins[left_m] - 1, ty + margins[up_m] - 1, xpos + margins[left_m] - 1, ypos + margins[up_m] - 1,
                    width - margins[left_m] - margins[right_m] + 2, height - margins[up_m] - margins[down_m] + 2);
    }

    set_clip_state(portrait_display_buf, 0);
    blit(portrait_bmp_buf, portrait_display_buf, 0, 0, portrait.x, portrait.y, portrait.tw * 16, portrait.th * 16);
}

// The string's parts put together in msgdisplaybuf: the frame, the portrait over it, the text
// over both, each once it's been put on its buffer (its clip state, as upstream). msgdisplaybuf
// is what the rest of the game checks and draws (scrolling, the old intro); draw_screen and
// updatescr draw the parts themselves (q30_msg_blit), translucent ones as upstream.
// (msgdisplaybuf's clip state is the game's "something to draw" flag, so it's never drawn on
// with the sprite functions, which wouldn't clip with it off.)
void compose()
{
    clear_bitmap(msgdisplaybuf);
    bool any = false;

    if(!bg_display_buf->clip)
    {
        masked_blit(bg_display_buf, msgdisplaybuf, 0, 0, 0, 0, 256, 224);
        any = true;
    }

    if(!portrait_display_buf->clip)
    {
        masked_blit(portrait_display_buf, msgdisplaybuf, 0, 0, 0, 0, 256, 224);
        any = true;
    }

    if(!txt_display_buf->clip)
    {
        masked_blit(txt_display_buf, msgdisplaybuf, 0, 0, 0, 0, 256, 224);
        any = true;
    }

    set_clip_state(msgdisplaybuf, any ? 0 : 1);
}

// upstream's blit_bg/blit_fg: a display buffer onto dest, through trans_table2 when the
// string's flag asks for it
void blit_part(BITMAP *part, BITMAP *dest, int dy, int h, bool trans)
{
    if(part->clip)
        return;

    if(!trans)
    {
        masked_blit(part, dest, 0, 0, 0, dy, 256, h);
        return;
    }

    BITMAP *sub = create_bitmap_ex(8, 256, h);

    if(!sub)
        return;

    clear_bitmap(sub);
    masked_blit(part, sub, 0, 0, 0, 0, 256, h);
    COLOR_MAP *keep = color_map;
    color_map = &trans_table2;
    draw_trans_sprite(dest, sub, 0, dy);
    color_map = keep;
    destroy_bitmap(sub);
}

void clear_display_bmps()
{
    clear_bitmap(txt_display_buf);
    set_clip_state(txt_display_buf, 1);
    clear_bitmap(bg_display_buf);
    set_clip_state(bg_display_buf, 1);
    clear_bitmap(portrait_display_buf);
    set_clip_state(portrait_display_buf, 1);
    clear_bitmap(msgdisplaybuf);
    set_clip_state(msgdisplaybuf, 1);
}

void clear_bmps()
{
    clear_bitmap(txt_bmp_buf);
    clear_bitmap(bg_bmp_buf);
    clear_bitmap(portrait_bmp_buf);
    clear_bitmap(menu_bmp_buf);
    clear_display_bmps();
}

bool runMenuCursor()
{
    clear_bitmap(menu_bmp_buf);

    if(menu_options.empty())
    {
        msg_menu_data[MNU_CHOSEN] = 0;
        return true;    // the menu is over
    }

    int pos = msg_menu_data[MNU_CHOSEN];

    // a cursor on no choice goes to the first one from 0 up
    if(menu_options.find(pos) == menu_options.end())
    {
        pos = 0;
        while(menu_options.find(pos) == menu_options.end())
            ++pos;
    }

    MenuChoice *ch = &menu_options[pos];

    bool pressed = true;
    if(rUp()) pos = ch->upos;
    else if(rDown()) pos = ch->dpos;
    else if(rLeft()) pos = ch->lpos;
    else if(rRight()) pos = ch->rpos;
    else pressed = false;

    if(pressed)
        msg_menu_data[MNU_TIMER] = 1;

    const bool hold_input = !((msg_menu_data[MNU_TIMER]++) % 20);
    bool held = false;

    if(hold_input)
    {
        held = true;
        if(Up()) pos = ch->upos;
        else if(Down()) pos = ch->dpos;
        else if(Left()) pos = ch->lpos;
        else if(Right()) pos = ch->rpos;
        else held = false;
    }

    if(menu_options.find(pos) == menu_options.end())
    {
        pos = 0;
        while(menu_options.find(pos) == menu_options.end())
            ++pos;
    }

    if((pressed || held) && pos != msg_menu_data[MNU_CHOSEN])
        sfx(get_str().menu_move_sfx);

    if(!msg_menu_data[MNU_CAN_CONFIRM])     // no confirming at once while A is still held
    {
        rAbtn();    // (eaten)
        if(!cAbtn())
            msg_menu_data[MNU_CAN_CONFIRM] = 1;
    }

    const bool ret = (pressed || held) ? false : rAbtn();
    // eat the inputs
    rUp();
    rDown();
    rLeft();
    rRight();
    rAbtn();

    ch = &menu_options[pos];
    ensure_scrolled_to(ch->y - margins[up_m], msg_menu_data[MNU_CURSOR_HEI]);

    overtileblock16(menu_bmp_buf, msg_menu_data[MNU_CURSOR_TILE], ch->x, ch->y, ceil16(msg_menu_data[MNU_CURSOR_WID]),
                    ceil16(msg_menu_data[MNU_CURSOR_HEI]), msg_menu_data[MNU_CURSOR_CSET], msg_menu_data[MNU_CURSOR_FLIP]);

    msg_menu_data[MNU_CHOSEN] = pos;

    if(ret)
    {
        // the final cursor stays on the text, for scrolling and such
        overtileblock16(txt_bmp_buf, msg_menu_data[MNU_CURSOR_TILE], ch->x, ch->y, ceil16(msg_menu_data[MNU_CURSOR_WID]),
                        ceil16(msg_menu_data[MNU_CURSOR_HEI]), msg_menu_data[MNU_CURSOR_CSET], msg_menu_data[MNU_CURSOR_FLIP]);
        menu_options.clear();
        just_confirmed_menu = true;
        sfx(get_str().menu_close_sfx);
    }

    return ret;
}

bool bottom_margin_clip()
{
    return layout_bottom_margin_clip(msg_cur_y, height, margins[down_m], scroll.can_scroll,
                                     qr(QR_OLD_STRING_EDITOR_MARGINS), qr(QR_STRING_FRAME_OLD_WIDTH_HEIGHT));
}

void wait_to_advance(bool next_string = false)
{
    if(do_end_str)
        linkedmsgclk = 1;
    else
    {
        wait_advance = !next_string;
        linkedmsgclk = just_confirmed_menu ? 1 : 1 + q255_msg_advance_delay;
    }

    // segmented mode's max_visible_pos
    int row;
    scroll.segment_crossed(scroll.target_scroll_pos, msg_cur_y - margins[up_m] + used_height_buf, &row);
}

bool next_line()
{
    used_height = used_height_buf;
    const int thei = zc_max(used_height, text_height(msgfont));
    used_height_buf = text_height(msgfont);
    const int ty = msg_cur_y - margins[up_m];
    msg_cur_y += thei + get_str().vspace;

    if(bottom_margin_clip())
        return false;

    msg_cur_x = margins[left_m];
    int row;

    if(scroll.segment_crossed(ty, msg_cur_y - margins[up_m] + used_height_buf, &row))
    {
        msg_cur_y = row + margins[up_m];
        if(menu_options.empty())    // no pause while menu choices are being placed
            wait_to_advance();
    }

    return true;
}

// ---- what the commands act on ----------------------------------------------------------------

void do_new(int str);
void dismiss();

// getitem() / ownership, as upstream's string codes: a newer quest's items 256 and up are real
// items too (up to q30_items_end(), owned through gamedata::get_item)
void give_item(int id)
{
    if(id >= 0 && id < q30_items_end())
        getitem(id, true);
}

bool has_item(int id)
{
    return id >= 0 && id < q30_items_end() && game->get_item(id);
}

// the owned bottle slots, as upstream's check_bottle_slots
void owned_slots(std::vector<int> &slots)
{
    for(int s = 0; s < 256; ++s)
        if(q30_bottle_item_for_slot(s) >= 0)
            slots.push_back(s);
}

bool has_bottle(int type, int quant)
{
    if(!quant)
        return true;

    std::vector<int> slots;
    owned_slots(slots);

    for(size_t q = 0; q < slots.size(); ++q)
        if(q30_bottle_slot(slots[q]) == type && !--quant)
            return true;

    return false;
}

void fill_bottle(int val, int replace, int quant)
{
    if(!quant)
        return;

    std::vector<int> slots;
    owned_slots(slots);

    for(size_t q = 0; q < slots.size(); ++q)
    {
        if(q30_bottle_slot(slots[q]) == replace)
        {
            q30_set_bottle_slot(slots[q], val);
            if(quant > 1)
            {
                --quant;
                continue;
            }
            return;
        }
    }
}

int cur_mi()
{
    return currmap * MAPSCRSNORMAL + homescr;
}

int mapind(int map, int screen)
{
    return map * MAPSCRSNORMAL + screen;
}

void msg_error(const char *format, int value)
{
    al_trace("SCC: ");
    al_trace(format, value);
}

// ---- parsemsgcode ------------------------------------------------------------------------------

bool parsemsgcode(const Command &command_in)
{
    Command command = command_in;
    int last_arg = 0;

    if(qr(QR_OLD_LONG_SCC_ARGS))
    {
        int idx = -1;

        switch(command.code)
        {
        case QMSGC_GOTOIFSCREEND: idx = 1; break;
        case QMSGC_SETSCREEND: idx = 3; break;
        case QMSGC_SETCURRENTSCREEND: idx = 1; break;
        case QMSGC_GOTOIFCREEND: idx = 3; break;
        }

        if(idx > -1)
            command.args[idx] = command.trunc(idx);     // zslongToFix(getTrunc()): the number, unscaled
    }

    switch(command.code)
    {
    case QMSGC_NEWLINE:
        next_line();
        if(wait_advance)
            return true;    // not wait_advance_unprocessed_iter, which would apply the newline twice
        ensure_scrolled_to(msg_cur_y - margins[up_m], used_height_buf);
        return true;

    case QMSGC_COLOUR:
        text_color = CSET(command.arg(0)) + command.arg(1);
        return true;

    case QMSGC_SHDCOLOR:
        shadow_color = CSET(command.arg(0)) + command.arg(1);
        return true;

    case QMSGC_SHDTYPE:
        shadow_type = command.arg(0);
        return true;

    case QMSGC_SPEED:
        speed = command.arg(0);
        return true;

    case QMSGC_CTRUP:
        game->change_counter(command.arg(1), command.arg(0));
        return true;

    case QMSGC_CTRDN:
        game->change_counter(-command.arg(1), command.arg(0));
        return true;

    case QMSGC_CTRSET:
        game->set_counter(vbound(command.arg(1), 0, game->get_maxcounter(command.arg(0))), command.arg(0));
        return true;

    case QMSGC_CTRUPPC:
    case QMSGC_CTRDNPC:
    case QMSGC_CTRSETPC:
    {
        const int counter = command.arg(0);
        double frac = command.arg(1) * 0.01;
        frac = frac < 0.0 ? 0.0 : (frac > 1.0 ? 1.0 : frac);
        int amount = (int)(frac * game->get_maxcounter(counter));

        if(command.code == QMSGC_CTRDNPC)
            amount *= -1;

        if(command.code == QMSGC_CTRSETPC)
            game->set_counter(amount, counter);
        else
            game->change_counter(amount, counter);

        return true;
    }

    case QMSGC_GIVEITEM:
        give_item(command.arg(0));
        return true;

    case QMSGC_COLLECTITEM:
    {
        const int id = command.arg(0);

        if(id < 0 || id >= q30_items_end())   // (items 256 and up are item sprites too)
            return true;

        if(items.Count() >= items.getMax())     // !items.has_space()
        {
            al_trace("SCC: No room to spawn item\n");
            return true;
        }

        // Link picks it up once the message lets him move (upstream's forcegrab, checkItems)
        additem(0, qr(QR_NOITEMOFFSET) ? 1 : 0, id, ipHOLDUP);
        g_collect_uids.push_back(items.spr(items.Count() - 1)->getUID());
        return true;
    }

    case QMSGC_WARP:
    {
        int dx = command.arg(2), dy = command.arg(3);
        if(dx >= 65023) dx = -1;
        if(dy >= 65023) dy = -1;
        q30_msg_script_warp(command.arg(0), command.arg(1), dx, dy, command.arg(4), command.arg(5));
        do_end_str = true;
        return true;
    }

    case QMSGC_SETSCREEND:
    {
        const int s = command.arg(1) + command.arg(0) * 128, reg = command.arg(2);
        if(s >= 0 && s < MAXDMAPS * MAPSCRSNORMAL && reg >= 0 && reg < 8)
            game->screen_d[s][reg] = command.zlong(3);
        return true;
    }

    case QMSGC_SETCURRENTSCREEND:
    {
        const int s = currscr + currdmap * 128, reg = command.arg(0);
        if(s >= 0 && s < MAXDMAPS * MAPSCRSNORMAL && reg >= 0 && reg < 8)
            game->screen_d[s][reg] = command.zlong(1);
        return true;
    }

    case QMSGC_TAKEITEM:
    {
        const int id = command.arg(0);
        if(id >= 0 && id < q30_items_end())
        {
            takeitem(id);
            verifyBothWeapons();
        }
        return true;
    }

    case QMSGC_SFX:
        sfx(command.arg(0));
        return true;

    case QMSGC_MIDI:
    {
        const int music = command.arg(0);
        if(music == 0)
            music_stop();
        else
            jukebox(music + (ZC_MIDI_COUNT - 1));
        return true;
    }

    case QMSGC_MUSIC:
    {
        // -1 the level's music, a music object, anything else stops it (q30_play_music: 0)
        const int music = command.arg(0);
        q30_play_music(music == -1 ? -1 : (music >= 1 && music <= q30_music_count() ? music : 0));
        return true;
    }

    case QMSGC_MUSIC_REFRESH:
    {
        const int type = command.arg(0);
        if(type >= 0 && type <= 4)      // MUSIC_UPDATE_SCREEN .. MUSIC_UPDATE_REGION
            q30_set_music_update_cond(type);
        return true;
    }

    case QMSGC_FONT:
    {
        const int oh = text_height(msgfont);
        msgfont = get_zc_font(command.arg(0));
        const int mh = zc_max(oh, text_height(msgfont));
        if(mh > used_height_buf)
            used_height_buf = mh;
        return true;
    }

    case QMSGC_RUN_FRZ_GENSCR:
    {
        just_confirmed_menu = false;
        const int scr_id = command.arg(0);

        if(command.arg(1) != 0)     // force_redraw
        {
            update();
            compose();
            draw_screen(tmpscr);
        }

        // arguments past the first two are the script's InitD
        long initd[8];
        int count = 0;
        memset(initd, 0, sizeof(initd));

        for(int q = 0; q < 8 && q + 2 < command.num_args; ++q, ++count)
            initd[q] = command.zlong(q + 2);

        q30_msg_run_generic_frozen(scr_id, initd, count);
        return true;
    }

    case QMSGC_DRAWTILE:
    {
        const int tl = command.arg(0), cs = command.arg(1), t_wid = command.arg(2), t_hei = command.arg(3), fl = command.arg(4);
        const Q30MsgStr &msgstring = get_str();

        if(layout_wrap_needed(msg_cur_x, msgstring.hspace, t_wid, width, margins[right_m]))
        {
            if(!next_line())
                return true;
            if(wait_advance)
            {
                wait_advance_unprocessed_iter = true;
                return true;
            }
        }

        just_confirmed_menu = false;
        used_height_buf = zc_max(used_height_buf, t_hei);
        ensure_scrolled_to(msg_cur_y - margins[up_m], used_height_buf);
        overtileblock16(txt_bmp_buf, tl, msg_cur_x, msg_cur_y, ceil16(t_wid), ceil16(t_hei), cs, fl);
        msg_cur_x += msgstring.hspace + t_wid;
        return true;
    }

    case QMSGC_GOTOIFRAND:
    {
        const int odds = command.arg(0);
        last_arg = 1;
        if(!odds || !(rand() % odds))
            goto switched;
        return true;
    }

    case QMSGC_GOTOIFSCREEND:
    {
        const int d = zc_min(7, command.arg(0));
        const int s = (currdmap << 7) + currscr - (DMaps[currdmap].type == dmOVERW ? 0 : DMaps[currdmap].xoff);

        if(d >= 0 && s >= 0 && s < MAXDMAPS * MAPSCRSNORMAL && game->screen_d[s][d] >= command.zlong(1))
        {
            last_arg = 2;
            goto switched;
        }
        return true;
    }

    case QMSGC_GOTOIFGLOBALSTATE:
    {
        const int index = command.arg(0);
        const bool value = command.args[1] != 0;

        if(index < 0 || index >= 256)
        {
            msg_error("Global state %d is invalid\n", index);
            return true;
        }

        if((q30_global_state(index) != 0) == value)
        {
            last_arg = 2;
            goto switched;
        }
        return true;
    }

    case QMSGC_SETGLOBALSTATE:
    {
        const int index = command.arg(0), value = command.arg(1);

        if(index < 0 || index >= 256)
        {
            msg_error("Global state %d is invalid\n", index);
            return true;
        }

        // a string toggles the state's combos whenever it changes (q30_set_global_state does
        // that for scripts unless qr_OLD_SCRIPT_LEVEL_GLOBAL_STATES)
        const bool changed = (q30_global_state(index) != 0) != (value != 0);
        q30_set_global_state(index, value);
        if(changed && q30_quest_rule(429))
            q30_trig_global_state(index);
        return true;
    }

    case QMSGC_GOTOIFSCREENSTATE:
    {
        const int flag = command.arg(0);
        const bool value = command.args[1] != 0;

        if(flag < 0 || flag >= mMAXIND)
        {
            msg_error("Screen state %d is invalid\n", flag);
            return true;
        }

        if(getmapflag(1 << flag) == value)
        {
            last_arg = 2;
            goto switched;
        }
        return true;
    }

    case QMSGC_GOTOIFANYSCREENSTATE:
    {
        int map = command.arg(0);
        const int screen = command.arg(1), flag = command.arg(2);
        const bool value = command.args[3] != 0;

        if(flag < 0 || flag >= mMAXIND)
        {
            msg_error("Screen state %d is invalid\n", flag);
            return true;
        }
        if(map < 1 || map > map_count)
        {
            msg_error("Map %d is invalid\n", map);
            return true;
        }
        if(screen < 0 || screen >= 0x80)
        {
            msg_error("Screen %d is invalid\n", screen);
            return true;
        }
        if(!qr(QR_BROKEN_SCC_MAP_ARGS))
            --map;      // 0-index

        if(((game->maps[mapind(map, screen)] & (1 << flag)) != 0) == value)
        {
            last_arg = 4;
            goto switched;
        }
        return true;
    }

    case QMSGC_GOTOIFSCREENEXSTATE:
    {
        const int flag = command.arg(0);
        const bool value = command.args[1] != 0;

        if(flag < 0 || flag >= 32)
        {
            msg_error("Screen ExState %d is invalid\n", flag);
            return true;
        }

        if(q30_get_exstate(cur_mi(), flag) == value)
        {
            last_arg = 2;
            goto switched;
        }
        return true;
    }

    case QMSGC_GOTOIFANYSCREENEXSTATE:
    {
        int map = command.arg(0);
        const int screen = command.arg(1), flag = command.arg(2);
        const bool value = command.args[3] != 0;

        if(flag < 0 || flag >= 32)
        {
            msg_error("Screen ExState %d is invalid\n", flag);
            return true;
        }
        if(map < 1 || map > map_count)
        {
            msg_error("Map %d is invalid\n", map);
            return true;
        }
        if(screen < 0 || screen >= 0x80)
        {
            msg_error("Screen %d is invalid\n", screen);
            return true;
        }
        if(!qr(QR_BROKEN_SCC_MAP_ARGS))
            --map;

        if(q30_get_exstate(mapind(map, screen), flag) == value)
        {
            last_arg = 4;
            goto switched;
        }
        return true;
    }

    case QMSGC_GOTOIFLEVELSTATE:
    {
        int level = command.arg(0);
        const int flag = command.arg(1);
        const bool value = command.args[2] != 0;

        if(level == -1)
            level = dlevel;
        if(level < 0 || level >= MAXLEVELS)
        {
            msg_error("Level %d is invalid\n", level);
            return true;
        }
        if(flag < 0 || flag >= 32)
        {
            msg_error("Level state %d is invalid\n", flag);
            return true;
        }

        if(((q30_level_state(level) & (1u << flag)) != 0) == value)
        {
            last_arg = 3;
            goto switched;
        }
        return true;
    }

    case QMSGC_SETLEVELSTATE:
    {
        int level = command.arg(0);
        const int flag = command.arg(1);
        const bool value = command.args[2] != 0;

        if(level == -1)
            level = dlevel;
        if(level < 0 || level >= MAXLEVELS)
        {
            msg_error("Level %d is invalid\n", level);
            return true;
        }
        if(flag < 0 || flag >= 32)
        {
            msg_error("Level state %d is invalid\n", flag);
            return true;
        }

        const dword bits = q30_level_state(level);

        if(level == dlevel)
        {
            // upstream only toggles the switches here
            if(((bits & (1u << flag)) != 0) != value)
                q30_trig_level_state(1u << flag);
        }
        else
            q30_set_level_state(level, value ? (bits | (1u << flag)) : (bits & ~(1u << flag)));
        return true;
    }

    case QMSGC_GOTOIFLEVELITEM:
    {
        int level = command.arg(0);
        const int flag = command.arg(1);
        const bool value = command.args[2] != 0;

        if(level == -1)
            level = dlevel;
        if(level < 0 || level >= MAXLEVELS)
        {
            msg_error("Level %d is invalid\n", level);
            return true;
        }
        if(flag < 0 || flag >= 16)
        {
            msg_error("Level item %d is invalid\n", flag);
            return true;
        }

        if(((game->lvlitems[level] & (1 << flag)) != 0) == value)
        {
            last_arg = 3;
            goto switched;
        }
        return true;
    }

    case QMSGC_SETLEVELITEM:
    {
        int level = command.arg(0);
        const int flag = command.arg(1);
        const bool value = command.args[2] != 0;

        if(level == -1)
            level = dlevel;
        if(level < 0 || level >= MAXLEVELS)
        {
            msg_error("Level %d is invalid\n", level);
            return true;
        }
        if(flag < 0 || flag >= 16)
        {
            msg_error("Level item %d is invalid\n", flag);
            return true;
        }

        if(value)
            game->lvlitems[level] |= (1 << flag);
        else
            game->lvlitems[level] &= ~(1 << flag);
        return true;
    }

    case QMSGC_GOTOIFSECRETS:
        if(q30_secrets_triggered())
        {
            last_arg = 0;
            goto switched;
        }
        return true;

    case QMSGC_GOTOIFCREEND:
    {
        const int s = command.arg(1) + (command.arg(0) << 7), reg = command.arg(2);

        if(s >= 0 && s < MAXDMAPS * MAPSCRSNORMAL && reg >= 0 && reg < 8 && game->screen_d[s][reg] >= command.zlong(3))
        {
            last_arg = 4;
            goto switched;
        }
        return true;
    }

    case QMSGC_GOTOIFITEM:
        if(has_item(command.arg(0)))
        {
            last_arg = 1;
            goto switched;
        }
        return true;

    case QMSGC_GOTOIFCTR:
        if((long)game->get_counter(command.arg(0)) * 10000 >= command.zlong(1))
        {
            last_arg = 2;
            goto switched;
        }
        return true;

    case QMSGC_GOTOIFCTRPC:
    {
        // zfix: (args[1] / 100) * max, as an int (rounded)
        const int counter = command.arg(0);
        const int64_t v = (int64_t)(command.zlong(1) / 100) * game->get_maxcounter(counter);
        int64_t r = (v % 10000) / 5000;
        const int amount = (int)(v / 10000 + (v < 0 ? (r > 0 ? 0 : r) : (r < 0 ? 0 : r)));

        if(game->get_counter(counter) >= amount)
        {
            last_arg = 2;
            goto switched;
        }
        return true;
    }

    case QMSGC_GOTOIFTRICOUNT:
        if((long)TriforceCount() * 10000 >= command.zlong(0))
        {
            last_arg = 1;
            goto switched;
        }
        return true;

    case QMSGC_GOTOIFTRI:
    {
        const int lev = command.arg(0);
        if(lev >= 0 && lev < MAXLEVELS && (game->lvlitems[lev] & liTRIFORCE))
        {
            last_arg = 1;
            goto switched;
        }
        return true;
    }

    case QMSGC_GOTOIFBOTTLE:
        if(has_bottle(command.arg(1), command.arg(0)))
        {
            last_arg = 2;
            goto switched;
        }
        return true;

    case QMSGC_CHANGEBOTTLE:
        fill_bottle(command.arg(2), command.arg(1), command.arg(0));
        return true;

    case QMSGC_CHANGEPORTRAIT:
        portrait.tile = command.arg(0);
        portrait.cset = (byte)command.arg(1);
        portrait.x = (byte)command.arg(2);
        portrait.y = (byte)command.arg(3);
        portrait.tw = (byte)command.arg(4);
        portrait.th = (byte)command.arg(5);
        draw_prt();
        return true;

    case QMSGC_SETUPMENU:
        for(int q = 0; q < 5; ++q)
            msg_menu_data[MNU_CURSOR_TILE + q] = command.arg(q);
        return true;

    case QMSGC_MENUCHOICE:
    {
        just_confirmed_menu = false;
        const Q30MsgStr &msgstring = get_str();

        if(layout_wrap_needed(msg_cur_x, msgstring.hspace, msg_menu_data[MNU_CURSOR_WID], width, margins[right_m]))
        {
            if(!next_line())
                return true;
            if(wait_advance)
            {
                wait_advance_unprocessed_iter = true;
                return true;
            }
        }

        used_height_buf = zc_max(used_height_buf, msg_menu_data[MNU_CURSOR_HEI]);
        ensure_scrolled_to(msg_cur_y - margins[up_m], used_height_buf);
        MenuChoice c = { msg_cur_x, msg_cur_y, command.arg(0), command.arg(1), command.arg(2), command.arg(3), command.arg(4) };
        menu_options[command.arg(0)] = c;
        msg_cur_x += msgstring.hspace + msg_menu_data[MNU_CURSOR_WID];
        return true;
    }

    case QMSGC_RUNMENU:
        just_confirmed_menu = false;
        msg_menu_data[MNU_CHOSEN] = 0;
        msg_menu_data[MNU_CAN_CONFIRM] = 0;
        if(menu_options.empty())
            return true;
        do_run_menu = true;
        return true;

    case QMSGC_GOTOMENUCHOICE:
        if(msg_menu_data[MNU_CHOSEN] == command.arg(0))
        {
            last_arg = 1;
            goto switched;
        }
        return true;

    case QMSGC_KILLHERO:
        game->set_life(0);      // (Hero->Kill(); this engine has no revive to bypass)
        return true;

    case QMSGC_ENDSTRING:
        do_end_str = true;
        return true;

    case QMSGC_WAIT_ADVANCE:
        wait_to_advance();
        return true;

    case QMSGC_DELAY:
    case QMSGC_FORCE_DELAY:
    {
        just_confirmed_menu = false;
        const int frames = command.arg(0);

        if(frames <= 0)
        {
            msg_error("frames %d is invalid (must be >0)\n", frames);
            return true;
        }

        cur_iterator->set_post_segment_delay(frames, command.code == QMSGC_FORCE_DELAY);
        return true;
    }

    case QMSGC_TRIGSECRETS:
        hidden_entrance(0, true, false, -1);
        if(command.args[0] != 0)
            setmapflag(mSECRET);
        return true;

    case QMSGC_TRIG_CMB_COPYCAT:
    {
        const int copy_id = command.arg(0);
        if(copy_id == (byte)copy_id)
            q30_trig_copycat((byte)copy_id);
        return true;
    }

    case QMSGC_SETSCREENSTATE:
    {
        const int flag = command.arg(0);
        if(flag < 0 || flag >= mMAXIND)
        {
            msg_error("SCC 133: State %d is invalid\n", flag);
            return true;
        }
        if(flag >= Q30_MSG_STORED_STATES)
        {
            msg_error("SCC 133: State %d can't be stored by this player\n", flag);
            return true;
        }
        if(command.args[1] != 0)
            setmapflag(1 << flag);
        else
            unsetmapflag(cur_mi(), 1 << flag, true);
        return true;
    }

    case QMSGC_SETSCREENSTATER:
    {
        int map = command.arg(0);
        const int scrid = command.arg(1), flag = command.arg(2);

        if(map < 1 || map > map_count)
        {
            msg_error("SCC 134: Map %d is invalid\n", map);
            return true;
        }
        if(scrid < 0 || scrid >= 0x80)
        {
            msg_error("SCC 134: Screen %d is invalid\n", scrid);
            return true;
        }
        if(flag < 0 || flag >= mMAXIND)
        {
            msg_error("SCC 134: State %d is invalid\n", flag);
            return true;
        }
        if(flag >= Q30_MSG_STORED_STATES)
        {
            msg_error("SCC 134: State %d can't be stored by this player\n", flag);
            return true;
        }
        if(!qr(QR_BROKEN_SCC_MAP_ARGS))
            --map;

        if(command.args[3] != 0)
            setmapflag(mapind(map, scrid), 1 << flag);
        else
            unsetmapflag(mapind(map, scrid), 1 << flag, true);
        return true;
    }

    case QMSGC_SETANYSCREENEXSTATE:
    {
        int map = command.arg(0);
        const int screen = command.arg(1), flag = command.arg(2);

        if(flag < 0 || flag >= 32)
        {
            msg_error("Screen ExState %d is invalid\n", flag);
            return true;
        }
        if(map < 1 || map > map_count)
        {
            msg_error("Map %d is invalid\n", map);
            return true;
        }
        if(screen < 0 || screen >= 0x80)
        {
            msg_error("Screen %d is invalid\n", screen);
            return true;
        }
        if(!qr(QR_BROKEN_SCC_MAP_ARGS))
            --map;

        q30_set_exstate(mapind(map, screen), flag, command.args[3] != 0);
        return true;
    }

    case QMSGC_SETSCREENEXSTATE:
    {
        const int flag = command.arg(0);
        if(flag < 0 || flag >= 32)
        {
            msg_error("Screen ExState %d is invalid\n", flag);
            return true;
        }
        q30_set_exstate(cur_mi(), flag, command.args[1] != 0);
        return true;
    }
    }

    return false;

switched:
    {
        const int lev = command.arg(last_arg);

        if(lev && qr(QR_SCC_GOTO_RESPECTS_CONTFLAG) && (str_data(lev).stringflags & STRINGFLAG_CONT))
            setmsg(lev);
        else
        {
            do_new(lev);
            used_height_buf = text_height(msgfont);
        }

        just_confirmed_menu = false;
        putprices(false);
        return true;
    }
}

// parsemsgcode2: the commands that print text give it back
std::string parsemsgcode2(const Command &command)
{
    char buf[32];

    if(command.code == QMSGC_NAME)
        return game->get_name();

    if(command.code == QMSGC_COUNTER)
    {
        sprintf(buf, "%d", (int)game->get_counter(command.arg(0)));
        return buf;
    }

    if(command.code == QMSGC_MAXCOUNTER)
    {
        sprintf(buf, "%d", (int)game->get_maxcounter(command.arg(0)));
        return buf;
    }

    parsemsgcode(command);
    return "";
}

bool putmsgchar(bool play_sfx)
{
    if(bottom_margin_clip())
        return false;

    const Q30MsgStr &msgstring = get_str();

    // the rest of the word doesn't fit: a new line
    const char *rem_word = cur_iterator->remaining_word();
    const int tlength = text_length(msgfont, rem_word) + ((int)strlen(rem_word) * msgstring.hspace);

    if(layout_char_wrap_needed(msg_cur_x, tlength, width, margins[right_m], (msgstring.stringflags & STRINGFLAG_WRAP) != 0, strcmp(rem_word, " ") == 0))
    {
        if(!next_line())
            return false;
        if(wait_advance)
        {
            wait_advance_unprocessed_iter = true;
            return false;
        }
    }

    ensure_scrolled_to(msg_cur_y - margins[up_m], used_height_buf);
    just_confirmed_menu = false;

    if(play_sfx)
        sfx(msgstring.sfx);

    if(cur_iterator->character != " ")
        textout_styled_aligned_ex(txt_bmp_buf, msgfont, cur_iterator->character.c_str(), msg_cur_x, msg_cur_y, shadow_type, sstaLEFT, text_color, shadow_color, -1);

    msg_cur_x += text_length(msgfont, cur_iterator->character.c_str());
    if(cur_iterator->character != " ")
        msg_cur_x += msgstring.hspace;

    return true;
}

enum TickResult { tick_exit, tick_break, tick_continue };

void msg_tick_end(bool disappear = false);

TickResult msg_tick(bool play_sfx, bool burst_mode)
{
    if(cur_iterator->done())
        return tick_exit;

    if(!do_run_menu)
    {
        while(cur_iterator->state == Iterator::COMMAND && !do_run_menu)
        {
            const std::string text = parsemsgcode2(cur_iterator->command);

            // a command can dismiss the message (a warp); only the end-of-string work is left
            if(!cur_iterator)
            {
                if(do_end_str)
                    msg_tick_end();
                return tick_exit;
            }

            if(wait_advance || do_end_str)
                break;

            if(do_run_menu)
            {
                cur_iterator->set_buffer("");
                break;
            }

            if(text.empty())
            {
                cur_iterator->set_buffer("");
                cur_iterator->next();   // many commands in one frame
            }
            else
            {
                cur_iterator->set_buffer(text);
                cur_iterator->next();
            }
        }

        if(cur_iterator->state == Iterator::CHARACTER)
        {
            if(!putmsgchar(play_sfx))
                return tick_break;
        }
    }

    const bool check_early = !burst_mode;

    if(check_early && do_end_str)
    {
        msg_tick_end();
        return tick_exit;
    }

    if(do_run_menu)
    {
        if(!runMenuCursor())
            return tick_break;

        scroll.target_scroll_pos = scroll.max_scroll();
        do_run_menu = false;
    }

    if(!(wait_advance && wait_advance_unprocessed_iter))
        cur_iterator->next();

    if(!check_early && do_end_str)
    {
        msg_tick_end();
        return tick_exit;
    }

    if(cur_iterator->done())
    {
        const int nextstr = get_str().nextstring;
        if(nextstr && (str_data(nextstr).stringflags & STRINGFLAG_CONT))
            setmsg(nextstr);
    }

    if(wait_advance)
        return tick_break;

    return tick_continue;
}

void msg_tick_end(bool disappear)
{
    int nextstr = 0;

    if(disappear)
        goto disappear_now;

    // done printing the string
    if(do_end_str || (!do_run_menu && cur_iterator && (cur_iterator->done() || bottom_margin_clip()) && (wait_advance || !linkedmsgclk)))
    {
        if(do_end_str)
            just_confirmed_menu = false;

        if(!do_end_str && !wait_advance)
        {
            while(cur_iterator && cur_iterator->state == Iterator::COMMAND)
            {
                parsemsgcode2(cur_iterator->command);
                if(do_end_str || !cur_iterator)
                    break;
                cur_iterator->next();
            }
        }

        // go to the next string, or disappear by going to string 0
        nextstr = get_str().nextstring;

        if(nextstr || get_bit(quest_rules, qr_MSGDISAPPEAR) || q30_enqueued_str)
            wait_to_advance(true);

        if(!nextstr && !wait_advance)
        {
            if(!get_bit(quest_rules, qr_MSGDISAPPEAR))
            {
disappear_now:
                msg_active = false;
                Link.finishedmsg();
            }

            if(repaircharge)
            {
                game->change_drupy(-tmpscr[currscr < 128 ? 0 : 1].catchall);
                repaircharge = 0;
            }

            if(adjustmagic)
            {
                if(qr(QR_OLD_HALF_MAGIC))
                {
                    if(game->get_magicdrainrate())
                        game->set_magicdrainrate(1);
                }
                else if(game->get_magicdrainrate() > 1)
                    game->set_magicdrainrate(game->get_magicdrainrate() / 2);

                adjustmagic = false;
                sfx(WAV_SCALE);
                setmapflag((currscr < 128 && get_bit(quest_rules, qr_ITEMPICKUPSETSBELOW)) ? mITEM : mSPECIALITEM);
            }

            if(learnslash)
            {
                game->set_canslash(1);
                learnslash = false;
                sfx(WAV_SCALE);
                setmapflag((currscr < 128 && get_bit(quest_rules, qr_ITEMPICKUPSETSBELOW)) ? mITEM : mSPECIALITEM);
            }
        }
    }
}

void msg_consume_spaces()
{
    while(cur_iterator->character == " ")
    {
        if(!putmsgchar(false))
            break;

        cur_iterator->next();

        // the "Continue From Previous" feature
        if(cur_iterator->state == Iterator::DONE)
        {
            const int nextstr = get_str().nextstring;
            if(nextstr && (str_data(nextstr).stringflags & STRINGFLAG_CONT))
                setmsg(nextstr);
        }
    }
}

void reset_hint_icons()
{
    for(int i = 0; i < 3; ++i)
    {
        icon_anim[i][0] = icon_anim[i][1] = 0;
        icon_anim_live[i] = false;
    }
}

void tick_message()
{
    if(wait_advance && linkedmsgclk < 1)
        linkedmsgclk = 1;

    scroll.tick();

    if(linkedmsgclk > 0)
    {
        if(linkedmsgclk == 1)
        {
            if(scroll.can_scroll && !do_end_str && scroll.scroll_pos == scroll.target_scroll_pos)
            {
                const bool up = scroll.segmented ? rUp() : Up();
                const bool down = scroll.segmented ? rDown() : Down();
                scroll.scroll_input(up, down);
            }

            bool advance = do_end_str;

            if(!advance && just_confirmed_menu)
            {
                scroll.target_scroll_pos = scroll.max_scroll();
                advance = true;
            }

            if(!advance && scroll.at_max())
                advance = cAbtn() || cBbtn();

            if(advance)
            {
                if(!(do_end_str || just_confirmed_menu))
                    sfx(get_str().adv_sfx);

                do_end_str = just_confirmed_menu = false;
                linkedmsgclk = 0;

                if(wait_advance)
                    wait_advance = wait_advance_unprocessed_iter = false;
                else
                {
                    msgstr = get_str().nextstring;
                    used_height_buf = text_height(msgfont);

                    if(!msgstr && q30_enqueued_str)
                    {
                        msgstr = q30_enqueued_str;
                        q30_enqueued_str = 0;
                    }

                    if(!msgstr)
                    {
                        if(tmpscr->room != rGRUMBLE)
                            blockpath = false;

                        dismiss();
                        msg_tick_end(true);
                        return;
                    }

                    do_new(msgstr);
                    putprices(false);
                }
            }
        }
        else
            --linkedmsgclk;
    }

    if(wait_advance)
        return;     // waiting for a button

    if(!do_run_menu && (!msgstr || !cur_iterator || cur_iterator->done() || bottom_margin_clip()))
    {
        msg_active = false;
        reset_iterator(-1);
        return;
    }

    if(!cur_iterator)
        return;

    msg_onscreen = true;    // now the message is on the screen (see do_new)

    if(cur_iterator->state == Iterator::NOT_STARTED)
    {
        cur_iterator->next();
        msg_consume_spaces();

        // a string with no text to print is done at once, with the usual end-of-string work
        if(cur_iterator->done())
        {
            msg_tick_end();
            return;
        }
    }

    // B held (where the quest allows it) or speed 0: as many characters as possible at once
    if((cBbtn() && get_bit(quest_rules, qr_ALLOWMSGBYPASS)) || speed == 0)
    {
        while(!cur_iterator->done())
        {
            if(speed && !(cBbtn() && get_bit(quest_rules, qr_ALLOWMSGBYPASS)))
                goto breakout;  // the speed changed to non-zero
            if(cur_iterator->post_segment_delay && cur_iterator->post_segment_delay_forced)
                goto breakout;  // or a ForceDelay was reached

            const TickResult tick = msg_tick(cur_iterator->character != " ", true);

            if(tick == tick_break)
                break;
            if(tick == tick_exit)
                return;
        }

        if(!do_run_menu)
            msgclk = 72;
    }
    else
    {
breakout:
        word tempspeed = speed;
        const bool go_fast = get_bit(quest_rules, qr_ALLOWFASTMSG) && cAbtn();

        if(do_run_menu)
            tempspeed = 0;

        if(cur_iterator->post_segment_delay && cur_iterator->peek(0).empty())
        {
            tempspeed = 0;
            cur_iterator->post_segment_delay_fast = go_fast;
        }

        if((msgclk++ % (tempspeed + 1) < tempspeed) && !go_fast)
            return;
    }

    // the next tick: one character or one command (or, as a menu ends, both)
    if(!cur_iterator->done() && !bottom_margin_clip() && !wait_advance)
    {
        const TickResult tick = msg_tick(true, false);

        if(tick == tick_break)
        {
            msg_tick_end();
            return;
        }

        if(tick == tick_exit)
            return;

        // two spaces next: all the coming spaces now
        if(!cur_iterator->done() && cur_iterator->peek(0) == " " && cur_iterator->peek(1) == " ")
            msg_consume_spaces();
    }

    msg_tick_end();
}

// draw_icon: the icon's offset from the anchor it names, on the box or the screen
void draw_icon(const Q30MsgIcon &icon, int type)
{
    if(icon.sprite < 0 || icon.sprite >= MAXWPNS || !wpnsbuf[icon.sprite].tile)
        return;

    int x = icon.x, y = icon.y;
    const Q30MsgStr &s = get_str();

    // message_anchor: up, down, left, right, l_up, r_up, l_down, r_down, center, screen, screen_y_offset
    switch(icon.anchor)
    {
    case 4: case 2: case 6: x += s.x; break;
    case 0: case 8: case 1: x += s.x + s.w / 2; break;
    case 5: case 3: case 7: x += s.x + s.w; break;
    }

    switch(icon.anchor)
    {
    case 10: y += s.y; break;
    case 4: case 0: case 5: y += s.y; break;
    case 2: case 8: case 3: y += s.y + s.h / 2; break;
    case 6: case 1: case 7: y += s.y + s.h; break;
    }

    y += playing_field_offset;

    if(x < 0 && y < 0)
        return;

    // drawn as the "more" weapon (wPhantom, pMESSAGEMORE) with this sprite's graphics, each
    // icon type with its own animation clock (upstream's lens_hint_sprite)
    const wpndata keep = wpnsbuf[iwMore];
    wpnsbuf[iwMore] = wpnsbuf[icon.sprite];
    if(type == icon_more && qr(QR_NO_FLASHING_MSG_MORE))
        wpnsbuf[iwMore].misc &= ~3;     // no flashing (weapon::LOADGFX takes flash from these bits)
    putweapon(framebuf, x, y, wPhantom, pMESSAGEMORE, up, icon_anim[type][0], icon_anim[type][1], -1);
    wpnsbuf[iwMore] = keep;
    icon_anim_live[type] = true;
}

// an icon not drawn still animates, so it isn't out of step when it shows again
void dummy_animate_icon(const Q30MsgIcon &icon, int type)
{
    if(icon.sprite < 0 || icon.sprite >= MAXWPNS || !wpnsbuf[icon.sprite].tile)
        return;

    static BITMAP *scratch = NULL;

    if(!scratch)
        scratch = create_bitmap_ex(8, 64, 64);

    const wpndata keep = wpnsbuf[iwMore];
    wpnsbuf[iwMore] = wpnsbuf[icon.sprite];
    putweapon(scratch, 0, 0, wPhantom, pMESSAGEMORE, up, icon_anim[type][0], icon_anim[type][1], -1);
    wpnsbuf[iwMore] = keep;
}

const Q30MsgIcon &icon_of(const Q30MsgStr &s, int type)
{
    // init data older than v49 gives every string its "more" icon (qst_initdata.cpp)
    static Q30MsgIcon from_init;

    if(type == icon_more && q255_initdata_version < 49)
    {
        from_init.sprite = iwMore;
        from_init.anchor = zinit.msg_more_is_offset ? 10 : 9;   // screen_y_offset, screen
        from_init.x = zinit.msg_more_x;
        from_init.y = zc_min(160, (int)zinit.msg_more_y);
        return from_init;
    }

    return s.icons[type];
}

void draw_icons()
{
    if(!msgstr || linkedmsgclk != 1 || do_end_str)
        return;

    const Q30MsgStr &s = get_str();
    bool draw_more = false, draw_scr_up = false, draw_scr_down = false;

    if(!scroll.can_scroll || scroll.at_max())
        draw_more = true;

    if(scroll.can_scroll && scroll.active_speed)
    {
        draw_scr_up = scroll.cur_scroll() != 0;
        draw_scr_down = scroll.cur_scroll() < scroll.max_scroll();
    }

    if(draw_more)
        draw_icon(icon_of(s, icon_more), icon_more);
    else
        dummy_animate_icon(icon_of(s, icon_more), icon_more);

    if(draw_scr_up)
        draw_icon(icon_of(s, icon_scroll_up), icon_scroll_up);
    else
        dummy_animate_icon(icon_of(s, icon_scroll_up), icon_scroll_up);

    if(draw_scr_down)
        draw_icon(icon_of(s, icon_scroll_down), icon_scroll_down);
    else
        dummy_animate_icon(icon_of(s, icon_scroll_down), icon_scroll_down);
}

void do_new(int str)
{
    static const bool trace = getenv("ZC_Q30_MSGLOG") != NULL;    // diagnostics only

    if(trace)
        al_trace("q30 msg: new string %d (frame %d)\n", str, frame);

    if(msg_onscreen || msg_active)
        dismiss();

    linkedmsgclk = 0;
    msg_active = true;
    // not on the screen just yet
    setmsg(str);
    text_color = QMisc.colors.msgtext;
    speed = zinit.msg_speed;
    const Q30MsgStr &msgstring = get_str();
    layer = msgstring.drawlayer;
    nofreeze = (msgstring.stringflags & Q30_STRINGFLAG_NOFREEZE) != 0;
    scroll.can_scroll = !qr(QR_STRINGS_DONT_SCROLL) && !qr(QR_OLD_STRING_EDITOR_MARGINS);
    scroll.segmented = scroll.can_scroll && qr(QR_STRING_SEGMENTED_SCROLL);
    scroll.active_scrolling = false;
    used_height_buf = text_height(msgfont);
    scroll.scroll_pos = scroll.target_scroll_pos = scroll.max_visible_pos = 0;
    scroll.passive_speed = msgstring.passive_scroll_speed;
    scroll.active_speed = msgstring.active_scroll_speed;
    reset_hint_icons();

    clear_bmps();

    msgclk = 0;
    width = msgstring.w;
    height = msgstring.h;
    xpos = msgstring.x;
    ypos = msgstring.y;
    portrait.tile = msgstring.portrait_tile;
    portrait.cset = msgstring.portrait_cset;
    portrait.x = msgstring.portrait_x;
    portrait.y = msgstring.portrait_y;
    portrait.tw = msgstring.portrait_tw;
    portrait.th = msgstring.portrait_th;
    shadow_type = msgstring.shadow_type;
    shadow_color = msgstring.shadow_color;

    draw_bg(msgstring);
    draw_prt();

    const short *copy_from = qr(QR_OLD_STRING_EDITOR_MARGINS) ? old_margins : msgstring.margins;
    for(int q = 0; q < 4; ++q)
        margins[q] = copy_from[q];

    msg_cur_x = margins[left_m];
    msg_cur_y = margins[up_m];
    scroll.set_body_height(height, margins[up_m], margins[down_m]);
}

// called to make a message disappear
void dismiss()
{
    linkedmsgclk = 0;
    msgstr = msgclk = 0;
    reset_iterator(-1);
    xpos = ypos = 0;
    msg_cur_x = msg_cur_y = 0;
    portrait.clear();
    msg_onscreen = msg_active = false;
    nofreeze = false;
    msgfont = get_zc_font(0);   // font_zfont
    clear_display_bmps();
    do_end_str = false;
    wait_advance = wait_advance_unprocessed_iter = false;
    just_confirmed_menu = false;
    do_run_menu = false;
    menu_options.clear();
    memset(msg_menu_data, 0, sizeof(msg_menu_data));
    layer = 6;
    shadow_type = 0;
    shadow_color = 0;
    q30_enqueued_str = 0;
    speed = zinit.msg_speed;
    for(int q = 0; q < 4; ++q)
        margins[q] = old_margins[q];
    scroll.clear();
    reset_hint_icons();
}

// CollectItem's items: picked up once the message lets Link move (upstream's forcegrab items,
// which Hero::checkItems takes wherever they are)
void collect_items()
{
    if(g_collect_uids.empty() || (msg_active && !nofreeze && get_bit(quest_rules, qr_MSGFREEZE)))
        return;

    // Upstream's checkitems: a held-up item waits while Link is still holding one up or a linked
    // string is still to come (linked_clk). Taken at once, it closed the box at the end of the
    // string and the strings linked after it never showed (Mystical Harps' trade, strings
    // 1006 -> 1007).
    const int act = Link.getAction();
    const bool holding = act == landhold1 || act == landhold2 || act == waterhold1 || act == waterhold2;
    std::vector<long> uids;
    uids.swap(g_collect_uids);

    for(size_t i = 0; i < uids.size(); ++i)
        for(int j = 0; j < items.Count(); ++j)
            if(items.spr(j)->getUID() == uids[i])
            {
                if((((item *)items.spr(j))->pickup & ipHOLDUP) && (holding || linkedmsgclk))
                    g_collect_uids.push_back(uids[i]);      // next frame
                else
                    Link.checkitems(j);
                break;
            }
}

} // namespace

// ---- the game's side -------------------------------------------------------------------------

bool q30_msg_engine()
{
    return q30_active() && !g_strings.empty();
}

void q30_msg_clear_strings()
{
    g_strings.clear();
    g_parsed.clear();
}

void q30_msg_set_rules(bool old_frame, bool old_margins)
{
    g_old_frame = old_frame;
    g_old_margins = old_margins;
}

void q30_msg_set_string(int str, const Q30MsgStr &s)
{
    if(str < 0 || str >= 65536)
        return;

    if((size_t)str >= g_strings.size())
        g_strings.resize((size_t)str + 1);

    g_strings[str] = s;
    g_parsed.erase(str);
}

// A script set the text (messagedata->Set, Game->SetMessage).
void q30_msg_set_text(int str, const std::string &text, bool ascii)
{
    if(str < 0 || (size_t)str >= g_strings.size())
        return;

    g_strings[str].text = text;
    g_strings[str].ascii = ascii;
    g_parsed.erase(str);
}

void q30_msg_do_new(int str)
{
    if(!allocate_bmps())
        return;

    do_new(str);
}

void q30_msg_dismiss()
{
    if(!allocate_bmps())
        return;

    if(getenv("ZC_Q30_MSGLOG"))     // diagnostics only
        al_trace("q30 msg: dismissed (string %d, frame %d)\n", (int)msgstr, frame);

    dismiss();
}

// upstream's msgstr::run: one frame of the message, then the display buffers
void q30_msg_run()
{
    if(!allocate_bmps())
        return;

    // ZC_Q30_MSGTEST=str[,frame] (test runs only): shows that string once the game reaches the
    // frame (60 by default), to look at strings no screen shows on its own
    static const char *test = getenv("ZC_Q30_MSGTEST");
    static bool tested = false;

    if(test && !tested)
    {
        int str = 0, at = 60;
        sscanf(test, "%d,%d", &str, &at);
        if(frame >= at)
        {
            tested = true;
            do_new(str);
        }
    }

    tick_message();
    update();

    if(msgstr)
        compose();

    collect_items();
}

void q30_msg_draw_icons()
{
    if(allocate_bmps())
        draw_icons();
}

// A part of the string is on its display buffer (its clip state, as upstream)
static bool any_part_shown()
{
    return !bg_display_buf->clip || !portrait_display_buf->clip || !txt_display_buf->clip;
}

// upstream's msgstr::blit_str at the top of the playing field (y dy, h rows): the frame, the
// portrait and the text, translucent where the string says. False for 2.53 quests, which draw
// msgdisplaybuf as before, and while no string is up: msgdisplaybuf may then hold the old
// 72-character DMap intro (putintro writes it there), which the caller draws as before.
bool q30_msg_blit(BITMAP *dest, int dy, int h)
{
    if(!q30_msg_engine() || !allocate_bmps() || !any_part_shown())
        return false;

    const Q30MsgStr &s = get_str();
    blit_part(bg_display_buf, dest, dy, h, (s.stringflags & Q30_STRINGFLAG_TRANS_BG) != 0);
    blit_part(portrait_display_buf, dest, dy, h, false);
    blit_part(txt_display_buf, dest, dy, h, (s.stringflags & Q30_STRINGFLAG_TRANS_FG) != 0);
    return true;
}

// draw_screen's string draws for 2.55/3.0 quests, where upstream 3.0's draw_msgstr calls go
// (its maps.cpp). Before the passive subscreen (after_subscreen false): a layer 6 string unless
// qr_LAYER6_STRINGS_OVER_SUBSCREEN, and the lower layers, which upstream draws among the
// layers. After the subscreen and layer 7's script drawing: a layer 6 string again (twice with
// that rule; upstream draws it both times, which shows on translucent strings), then a layer 7
// string. False for 2.53 quests and while no string is up (as q30_msg_blit).
bool q30_msg_blit_screen(BITMAP *dest, int dy, int h, bool after_subscreen)
{
    if(!q30_msg_engine() || !allocate_bmps() || !any_part_shown())
        return false;

    const bool l6_over = qr(QR_LAYER6_STRINGS_OVER_SUBSCREEN);
    int draws = 0;

    if(!after_subscreen)
        draws = (layer < 6 || (layer == 6 && !l6_over)) ? 1 : 0;
    else if(layer == 6)
        draws = l6_over ? 2 : 1;
    else if(layer == 7)
        draws = 1;

    for(int i = 0; i < draws; ++i)
        q30_msg_blit(dest, dy, h);

    return true;
}

void q30_msg_clear_display()
{
    if(allocate_bmps())
        clear_display_bmps();
}

// upstream's init_string_vars, as a game starts
void q30_msg_init()
{
    if(!allocate_bmps())
        return;

    dismiss();
    g_collect_uids.clear();

    if(txt_bmp_height != 512 + 16)
    {
        txt_bmp_height = 512 + 16;
        destroy_bitmap(txt_bmp_buf);
        txt_bmp_buf = create_bitmap_ex(8, txt_bmp_width, txt_bmp_height);
        destroy_bitmap(menu_bmp_buf);
        menu_bmp_buf = create_bitmap_ex(8, txt_bmp_width, txt_bmp_height);
    }
}

bool q30_msg_nofreeze()
{
    return q30_msg_engine() && nofreeze;
}

int q30_msg_layer()
{
    return layer;
}

// The 2.55 readers for the header, rules, strings and misc sections. See q255.h.
//
// Read and dropped (2.53 has nowhere to keep them):
//   header v4+: the version numbers, kept only as text for the window title
//   header v4+: build date year, month, day, hour, minute
//   header v4+: new_version_devsig, new_version_compilername, new_version_compilerversion
//   header v4+: product_name
//   header v4+: compilerid, compilerversionnumber first..fourth, developerid
//   header v4+: made_in_module_name, build_datestamp, build_timestamp
//   header v5+: build_timezone
//   header v6+: external_zinfo
//   rules v15+: quest_rules bytes 20..99, the rules added after 2.53, except the
//     two with a home in extra_rules and the frame rule the strings reader needs
//   rules v18+: compile_setting_version and the compiler settings map
//   strings v9+: text past MSGSIZE characters
//   strings v7+: margins[4]
//   strings v7+: portrait tile, cset, x, y, tw, th
//   strings v8+: shadow_type, shadow_color
//   strings v10+: drawlayer
//   strings v11+: text control codes this engine can't run (see Converted)
//   strings v13+: icon_more, icon_scroll_up, icon_scroll_down (sprite, anchor, x, y)
//   strings v13+: active_scroll_speed, passive_scroll_speed
//   strings v13+: adv_sfx, menu_move_sfx, menu_close_sfx
//   misc all: shops, infos and warp rings past this engine's 256, 256 and 9
//   misc v8+: shop str[3], the item info strings
//   misc v9+: questmisc[32], and 4096 bytes of retired string labels
//   misc v11+: zscript_last_compiled_version
//   misc v12+: sprites[256], the misc sprites (16 bits each from v21): kept for
//     q30_triggers.cpp (falling push blocks, the switch hook's poof)
//   misc v13+: bottle_types[64]
//   misc v13+: bottle_shop_types[256]
//   misc v14+: miscsfx[256] (16 bits each from v19)
//   misc v17+: save_menus[32] with their options, savemenu_game_over, savemenu_f6
//   misc v22+: each save menu option's script run args and instance values
//   misc v23+: save menu selected option offsets, option shadows, misc texts
//
// Narrowed:
//   strings v6+: tile is 32 bits in the file and a word here. A tile the message
//     frame can't draw in this engine is stored as 0 (no frame).
//   strings v12+: sfx is 16 bits in the file and a byte here. A sound past 255 is
//     stored as 0 (no sound).
//   misc v20+: shop items are 16 bits in the file and bytes here. An item past 255
//     can't exist in this engine, so that shop slot is left empty.
//
// Converted to what this engine expects:
//   rules v17+: the bit qr_NOGANONINTRO sits on was inverted upstream (compat rule
//     version 12), so it is flipped back. That version number is read by the
//     section loop and not handed over, so every v17+ file counts as past it.
//   rules: qr_BITMAPOFFSETFIX is cleared for builds before 42, as readrules does.
//   rules v15+: qr_SHORTDGNWALK and qr_BROKENCHARINTDRAWING go to extra_rules.
//   strings: from build 50, w and h are the frame's size unless the quest has
//     qr_STRING_FRAME_OLD_WIDTH_HEIGHT; here they are always 16 less than the frame.
//   strings v11+: the text is plain ASCII with commands such as \Speed\2\ and is
//     rewritten in the binary control code form guys.cpp reads. Commands guys.cpp
//     has no code for are removed from the text in every version.
//   strings v5..12: stringflags 0x04 and 0x08 are cleared, as readstrings does.

#include "precompiled.h" //always first

#include "q255.h"
#include "q30.h"
#include "q30_triggers.h"
#include "sfx.h"
#include "zsys.h"

#include <ctype.h>
#include <string.h>

extern MsgStr *MsgStrings;
extern int msg_strings_size;
extern byte quest_rules[QUESTRULES_SIZE];
extern byte extra_rules[EXTRARULES_SIZE];

// bytes of quest rules from rules version 15 on
static const size_t Q255_RULES_SIZE = 100;

// Rule bits past the 2.53 ones, numbered as engine/src/core/qrs.h numbers them.
static const int Q255_QR_SHORTDGNWALK = 193;
static const int Q255_QR_STRING_FRAME_OLD_WIDTH_HEIGHT = 205;
static const int Q255_QR_BROKENCHARINTDRAWING = 667;

// The last tile a message frame can start on: frame2x2 reads up to tile+21 and
// nothing downstream checks the range.
static const long Q255_LAST_FRAME_TILE = NEWMAXTILES - 22;

static const int Q255_MAX_SHOPS = 256;
static const int Q255_MAX_INFOS = 256;
static const int Q255_MAX_WARP_RINGS = 9;
static const int Q255_MAX_SAVE_MENUS = 32;
static const int Q255_MAX_ITEM_ID = 255;
static const int Q255_MAX_SFX_ID = 255;

// Set by the rules section, which comes before the strings section in every file.
// Starts true, which leaves a string's w and h as the file has them.
static bool g_old_string_frame = true;
static bool g_old_subscr = true;
static const int Q255_QR_OLD_SUBSCR = 422;

bool q30_passive_on_top()
{
    extern word quest_header_zelda_version;
    return quest_header_zelda_version >= 0x255 && !g_old_subscr;
}

// ---- message text ---------------------------------------------------------------

struct SccCommand
{
    const char *name;
    int code;
    int min_args, max_args;
};

// engine/src/components/scc/scc.cpp's scc_commands
static const SccCommand g_scc_commands[] =
{
    { "TextColor", 1, 2, 2 },
    { "Speed", 2, 1, 1 },
    { "GoIfScreenD", 3, 3, 3 },
    { "GoIfRand", 4, 2, 2 },
    { "GoIfItem", 5, 2, 2 },
    { "GoIfCounter", 6, 3, 3 },
    { "GoIfCounter%", 7, 3, 3 },
    { "GoIfMcGuffin", 8, 2, 2 },
    { "GoIfMcGuffinCount", 9, 2, 2 },
    { "CounterAdd", 10, 2, 2 },
    { "CounterSubtract", 11, 2, 2 },
    { "CounterSet", 12, 2, 2 },
    { "CounterAdd%", 13, 2, 2 },
    { "CounterSubtract%", 14, 2, 2 },
    { "CounterSet%", 15, 2, 2 },
    { "GiveItem", 16, 1, 1 },
    { "TakeItem", 17, 1, 1 },
    { "Warp", 18, 6, 6 },
    { "SetScreenD", 19, 4, 4 },
    { "SFX", 20, 1, 1 },
    { "MIDI", 21, 1, 1 },
    { "Name", 22, 0, 0 },
    { "GoIfAnyScreenD", 23, 5, 5 },
    { "Portrait", 24, 6, 6 },
    { "Newline", 25, 0, 0 },
    { "ShadowColor", 26, 2, 2 },
    { "ShadowType", 27, 1, 1 },
    { "Tile", 28, 5, 5 },
    { "EndString", 29, 0, 0 },
    { "WaitAdvance", 30, 0, 0 },
    { "SetupMenu", 128, 5, 5 },
    { "MenuChoice", 129, 5, 5 },
    { "RunMenu", 130, 0, 0 },
    { "GoIfMenuChoice", 131, 2, 2 },
    { "TriggerSecrets", 132, 1, 1 },
    { "SetScreenState", 133, 2, 2 },
    { "SetAnyScreenState", 134, 4, 4 },
    { "Font", 135, 1, 1 },
    { "RunFrozenGenericScript", 136, 2, 10 },
    { "TriggerComboCopycat", 137, 1, 1 },
    { "GoIfGlobalState", 138, 3, 3 },
    { "GoIfScreenState", 139, 3, 3 },
    { "GoIfAnyScreenState", 140, 5, 5 },
    { "GoIfScreenExState", 141, 3, 3 },
    { "GoIfAnyScreenExState", 142, 5, 5 },
    { "GoIfLevelState", 143, 4, 4 },
    { "GoIfLevelItem", 144, 4, 4 },
    { "GoIfSecrets", 145, 1, 1 },
    { "SetScreenExState", 146, 2, 2 },
    { "SetAnyScreenExState", 147, 4, 4 },
    { "SetGlobalState", 148, 2, 2 },
    { "SetLevelState", 149, 3, 3 },
    { "SetLevelItem", 150, 3, 3 },
    { "Counter", 151, 1, 1 },
    { "MaxCounter", 152, 1, 1 },
    { "KillHero", 153, 1, 1 },
    { "CollectItem", 154, 1, 1 },
    { "Delay", 155, 1, 1 },
    { "ForceDelay", 156, 1, 1 },
    { "GoIfBottle", 157, 3, 3 },
    { "ChangeBottle", 158, 3, 3 },
    { "Music", 159, 1, 1 },
    { "MusicRefresh", 160, 1, 1 },
    { "SetCurrentScreenD", 161, 2, 2 },
};

static const int Q255_SCC_COUNT = sizeof(g_scc_commands) / sizeof(g_scc_commands[0]);
static const int Q255_SCC_MAX_ARGS = 10;

// the largest argument the binary form can hold, and what a negative one becomes
static const int Q255_SCC_LARGEST_ARG = 65023;

static const SccCommand *find_scc_command(int code)
{
    for(int i = 0; i < Q255_SCC_COUNT; ++i)
    {
        if(g_scc_commands[i].code == code)
            return &g_scc_commands[i];
    }

    return NULL;
}

static const SccCommand *find_scc_command_named(const std::string &name)
{
    for(int i = 0; i < Q255_SCC_COUNT; ++i)
    {
        const char *known = g_scc_commands[i].name;

        if(strlen(known) != name.size())
            continue;

        size_t j = 0;

        while(j < name.size() && tolower((unsigned char)known[j]) == tolower((unsigned char)name[j]))
            ++j;

        if(j == name.size())
            return &g_scc_commands[i];
    }

    return NULL;
}

// Whether parsemsgcode in guys.cpp has a case for this code.
static bool scc_runs_here(int code)
{
    return (code >= 1 && code <= 17) || code == 20 || code == 21 || code == 25;
}

// Collects a string's text in the binary form, up to what MsgStr::s holds.
struct MsgWriter
{
    std::string text;
    bool full;

    MsgWriter() : full(false) {}

    void literal(char c)
    {
        if(text.size() >= (size_t)MSGSIZE)
        {
            full = true;
            return;
        }

        text += c;
    }

    // A command goes in whole or not at all, and not where parsemsgcode won't read it.
    void command(const std::string &bytes)
    {
        if(text.size() >= (size_t)(MSGSIZE - 2) || text.size() + bytes.size() > (size_t)MSGSIZE)
        {
            full = true;
            return;
        }

        text += bytes;
    }
};

// Steps over one argument of a binary command; see grab_next_argument in guys.cpp.
static bool skip_binary_argument(const std::string &str, size_t &i)
{
    if(i + 1 >= str.size())
        return false;

    ++i;

    if(i + 2 < str.size() && (byte)str[i + 1] == 255)
        i += 2;

    return true;
}

// Copies binary text, leaving out the commands this engine can't run along with
// their arguments (parse_legacy_binary_msg_str decides what is a command).
static void copy_binary_text(const std::string &str, MsgWriter &out)
{
    size_t last = 0;        // just past the last character that isn't a space

    for(size_t i = str.size(); i > 0; --i)
    {
        if(str[i - 1] != ' ')
        {
            last = i;
            break;
        }
    }

    size_t cur = 0;

    while(cur < last && !out.full)
    {
        byte c = (byte)str[cur];
        const SccCommand *defn = (c >= 32 && c <= 126) ? NULL : find_scc_command((int)c - 1);

        if(!defn)
        {
            out.literal(str[cur]);
            ++cur;
            continue;
        }

        size_t start = cur;

        for(int i = 0; i < defn->min_args; ++i)
        {
            if(!skip_binary_argument(str, cur))
                break;
        }

        ++cur;

        if(scc_runs_here(defn->code))
            out.command(str.substr(start, cur - start));
    }

    // the spaces at the end
    while(cur < str.size() && !out.full)
    {
        out.literal(str[cur]);
        ++cur;
    }
}

enum
{
    Q255_NUM_OVERFLOW = 1,
    Q255_NUM_UNDERFLOW = 2,
    Q255_NUM_TOO_MANY_DEC = 4,
    Q255_NUM_TOO_FEW_DEC = 8
};

struct SccNumber
{
    int whole;              // the value without its decimals
    size_t next;            // just past the last digit
    bool has_decimal;
    int errs;
};

// parse_ascii_number in scc.cpp. False if there is no number at start.
static bool parse_scc_number(const std::string &str, size_t start, SccNumber &out)
{
    const int max_int = 214748;
    const int min_int = -214748;

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

    int ipart = 0;
    int dpart = 0;
    int errs = 0;
    bool has_decimal = false;

    while(i < str.size() && isdigit((unsigned char)str[i]))
    {
        int digit = str[i] - '0';

        if(!errs)
        {
            if(negative)
            {
                if(ipart < min_int / 10 || (ipart == min_int / 10 && -digit < min_int % 10))
                {
                    errs |= Q255_NUM_UNDERFLOW;
                    ipart = min_int;
                }
                else
                    ipart = ipart * 10 - digit;
            }
            else
            {
                if(ipart > max_int / 10 || (ipart == max_int / 10 && digit > max_int % 10))
                {
                    errs |= Q255_NUM_OVERFLOW;
                    ipart = max_int;
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

        int place = 1000;
        int max_dpart = 9999;

        if(ipart == max_int)
            max_dpart = 3647;
        else if(ipart == min_int)
            max_dpart = 3648;

        if(errs & (Q255_NUM_OVERFLOW | Q255_NUM_UNDERFLOW))
            dpart = max_dpart;

        if(i >= str.size() || !isdigit((unsigned char)str[i]))
            errs |= Q255_NUM_TOO_FEW_DEC;
        else
        {
            while(i < str.size() && place && isdigit((unsigned char)str[i]))
            {
                int digit = str[i] - '0';

                if(!errs)
                {
                    dpart += place * digit;

                    if(dpart > max_dpart)
                    {
                        errs |= negative ? Q255_NUM_UNDERFLOW : Q255_NUM_OVERFLOW;
                        dpart = max_dpart;
                    }
                }

                ++i;
                place /= 10;
            }

            if(i < str.size() && !place && isdigit((unsigned char)str[i]))
            {
                errs |= Q255_NUM_TOO_MANY_DEC;

                while(i < str.size() && isdigit((unsigned char)str[i]))
                    ++i;
            }
        }
    }

    out.whole = ipart;
    out.next = i;
    out.has_decimal = has_decimal;
    out.errs = errs;
    return true;
}

static void skip_to_slash(const std::string &str, size_t &cur)
{
    while(cur < str.size() && str[cur] != '\\')
        ++cur;
}

// The few arguments upstream lets carry decimals; the rest make a command invalid.
static bool scc_arg_allows_decimal(int code, int arg)
{
    switch(code)
    {
    case 3:     // GoIfScreenD
    case 161:   // SetCurrentScreenD
        return arg == 1;

    case 19:    // SetScreenD
    case 23:    // GoIfAnyScreenD
        return arg == 3;

    case 136:   // RunFrozenGenericScript
        return arg >= 2 && arg < Q255_SCC_MAX_ARGS;
    }

    return false;
}

// parse_ascii_scc_command in scc.cpp, with str[cur] on the opening backslash and
// another character after it. Leaves cur past the command, valid or not.
static bool parse_scc_command(const std::string &str, size_t &cur, int &code, int *args, int &num_args)
{
    const size_t last = str.size();

    ++cur;

    bool has_code = false;
    bool decimal_code = false;
    int written_code = 0;

    if(isalpha((unsigned char)str[cur]))
    {
        size_t start = cur;
        skip_to_slash(str, cur);
        const SccCommand *named = find_scc_command_named(str.substr(start, cur - start));

        if(named)
        {
            written_code = named->code;
            has_code = true;
        }
    }
    else
    {
        SccNumber number;

        if(parse_scc_number(str, cur, number))
        {
            decimal_code = number.has_decimal;
            cur = number.next;
            written_code = number.whole;
            has_code = true;
        }
    }

    if(!has_code)
        skip_to_slash(str, cur);

    code = (byte)written_code;      // upstream's field is a byte too
    const SccCommand *defn = find_scc_command(code);
    int min_args = defn ? defn->min_args : 0;
    int max_args = defn ? defn->max_args : 0;

    int found = 0;
    int arg_errors = 0;
    bool non_numeric = false;
    bool double_slash = false;
    bool bad_decimal = false;

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

        int value = 0;
        SccNumber number;

        if(!parse_scc_number(str, cur, number))
        {
            non_numeric = true;
            skip_to_slash(str, cur);
        }
        else if(number.next < last && str[number.next] == '.')
        {
            non_numeric = true;
            cur = number.next;
            skip_to_slash(str, cur);
        }
        else
        {
            if(number.has_decimal && !scc_arg_allows_decimal(code, found))
            {
                bad_decimal = true;
                number.errs &= ~(Q255_NUM_TOO_FEW_DEC | Q255_NUM_TOO_MANY_DEC);
            }

            arg_errors |= number.errs;
            value = number.whole;
            cur = number.next;
        }

        if(found < Q255_SCC_MAX_ARGS)
            args[found] = value;

        ++found;
    }

    num_args = found < Q255_SCC_MAX_ARGS ? found : Q255_SCC_MAX_ARGS;

    bool missing_slash = cur == last || str[cur] != '\\';

    if(!missing_slash)
        ++cur;

    bool valid = code != 0 && !missing_slash && defn != NULL && found >= min_args && found <= max_args
                 && !arg_errors && !non_numeric && !double_slash && !bad_decimal && !decimal_code;

    if(cur == last)
        return valid;

    if(str[cur] == ' ')
        ++cur;
    else
        valid = false;

    return valid;
}

static void write_binary_command(int code, const int *args, int num_args, MsgWriter &out)
{
    std::string bytes;
    bytes += (char)((code % 254) + 1);

    for(int i = 0; i < num_args; ++i)
    {
        int arg = args[i];

        if(arg < 0 || arg > Q255_SCC_LARGEST_ARG)
            arg = Q255_SCC_LARGEST_ARG;

        bytes += (char)((arg % 254) + 1);

        if(arg >= 254)
        {
            bytes += (char)0xff;
            bytes += (char)(arg / 254);
        }
    }

    out.command(bytes);
}

// Writes str[start..end) as plain text. A byte under 32 would be a command in the
// binary form, so it can't be kept.
static void write_ascii_literal(const std::string &str, size_t &start, size_t end, MsgWriter &out)
{
    if(start != std::string::npos && end > start)
    {
        for(size_t i = start; i < end && !out.full; ++i)
        {
            if((byte)str[i] >= 32)
                out.literal(str[i]);
        }
    }

    start = std::string::npos;
}

// parse_ascii_msg_str and serialize_legacy in scc.cpp: text from string version 11
// on. A command that doesn't parse stays in the text as written, as it does upstream.
static void copy_ascii_text(const std::string &str, MsgWriter &out)
{
    const size_t none = std::string::npos;
    const size_t last = str.size();
    size_t cur = 0;
    size_t literal_start = 0;

    while(cur < last && !out.full)
    {
        if(literal_start == none)
            literal_start = cur;

        char c = str[cur];

        if(c == '\n')
        {
            write_ascii_literal(str, literal_start, cur, out);
            ++cur;
            write_binary_command(MSGC_NEWLINE, NULL, 0, out);
            continue;
        }

        if(c == '{')
        {
            write_ascii_literal(str, literal_start, cur, out);
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

            write_ascii_literal(str, literal_start, cur, out);
            ++cur;
            continue;
        }

        if(c != '\\')
        {
            ++cur;
            continue;
        }

        if(cur + 1 < last)
        {
            char next = str[cur + 1];

            if(next == '{' || next == '}')
            {
                write_ascii_literal(str, literal_start, cur, out);
                literal_start = cur + 1;
                cur += 2;
                continue;
            }

            if(next == '\\')
            {
                write_ascii_literal(str, literal_start, cur + 1, out);
                cur += 2;
                continue;
            }
        }

        if(cur + 1 == last)
        {
            write_ascii_literal(str, literal_start, cur, out);
            literal_start = cur;
            ++cur;
            write_ascii_literal(str, literal_start, cur, out);
            break;
        }

        size_t command_start = cur;
        int code = 0;
        int args[Q255_SCC_MAX_ARGS];
        int num_args = 0;

        if(parse_scc_command(str, cur, code, args, num_args))
        {
            write_ascii_literal(str, literal_start, command_start, out);

            if(scc_runs_here(code))
                write_binary_command(code, args, num_args, out);
        }
        else
            write_ascii_literal(str, literal_start, cur, out);
    }

    write_ascii_literal(str, literal_start, last, out);
}

// Fills t.s from a string's bytes as the file holds them.
static void store_message_text(MsgStr &t, const std::string &raw, bool ascii)
{
    MsgWriter out;
    const std::string text = raw.substr(0, raw.find('\0'));

    if(ascii)
        copy_ascii_text(text, out);
    else
        copy_binary_text(text, out);

    memcpy(t.s, out.text.data(), out.text.size());
    t.s[out.text.size()] = 0;
}

// ---- save menus -----------------------------------------------------------------

// p_getvar(script_config *): the script, the run args (p_getarr reads at most the
// 8 the array has, whatever count the file gives) and the instance values.
static void skip_script_config(Q255Section &s)
{
    s.skip(2);

    word args = s.igetw();
    s.skip((size_t)(args < 8 ? args : 8) * 4);

    word values = s.igetw();
    s.skip((size_t)values * (2 + 4));
}

static int skip_save_menus(Q255Section &s)
{
    byte count = s.getc();

    if(count > Q255_MAX_SAVE_MENUS)
        return qe_invalid;

    for(int i = 0; i < count && !s.failed; ++i)
    {
        if(s.getc() != 0)           // this menu is empty
            continue;

        s.getcstr();                // name
        s.skip(2 + 4 + 1);          // flags, cursor tile and cset
        s.skip(s.version < 19 ? 2 : 4);     // cursor and choose sfx
        s.skip(7);                  // bg color, hspace, vspace, opt x, y, text aligns
        s.skip(2 + 1);              // close frames, close flash rate
        s.skip(2);                  // music: a midi number before v18
        s.skip(4 + 3);              // bg tile, cset, tw, th

        if(s.version >= 23)
            s.skip(2 + 2);          // selected option x and y offsets

        byte options = s.getc();

        for(int j = 0; j < options && !s.failed; ++j)
        {
            s.getcstr();            // text
            s.skip(2 + 1 + 1 + 4);  // flags, color, picked color, font

            if(s.version >= 22)
                skip_script_config(s);
            else
                s.skip(2);          // script

            if(s.version >= 23)
                s.skip(3);          // shadow type, color, picked color
        }

        if(s.version >= 23)
        {
            byte texts = s.getc();

            for(int j = 0; j < texts && !s.failed; ++j)
            {
                s.getcstr();        // text
                s.skip(3 + 4 + 3);  // color, shadow, font, x, y, align
            }
        }
    }

    s.skip(2);                      // savemenu_game_over, savemenu_f6

    return s.failed ? qe_invalid : 0;
}

// Moves items with hasitem clear to the end of each shop, as readmisc does.
static void sort_shop_items(miscQdata &m)
{
    for(int i = 0; i < Q255_MAX_SHOPS; ++i)
    {
        shoptype &shop = m.shop[i];

        for(int j = 0; j < 2; ++j)
        {
            for(int k = 0; k < 2 - j; ++k)
            {
                if(shop.hasitem[k] == 0)
                {
                    byte item = shop.item[k];
                    shop.item[k] = shop.item[k + 1];
                    shop.item[k + 1] = item;

                    word price = shop.price[k];
                    shop.price[k] = shop.price[k + 1];
                    shop.price[k + 1] = price;

                    byte has = shop.hasitem[k];
                    shop.hasitem[k] = shop.hasitem[k + 1];
                    shop.hasitem[k + 1] = has;
                }
            }
        }
    }
}

// Moves empty strings to the end of each info shop, as readmisc does.
static void sort_info_strings(miscQdata &m)
{
    for(int i = 0; i < Q255_MAX_INFOS; ++i)
    {
        infotype &info = m.info[i];

        for(int j = 0; j < 2; ++j)
        {
            for(int k = 0; k < 2 - j; ++k)
            {
                if(info.str[k] == 0)
                {
                    word str = info.str[k];
                    info.str[k] = info.str[k + 1];
                    info.str[k + 1] = str;

                    word price = info.price[k];
                    info.price[k] = info.price[k + 1];
                    info.price[k + 1] = price;
                }
            }
        }
    }
}

// The ZC version this quest was built with, as text, for the window title.
char q255_quest_built_with[48];

// id: major, minor, patch, fourth, alpha, beta, gamma, release
static void note_built_with(const int *id, bool nightly, const std::string &text)
{
    char buf[80] = "";

    if(id[0] >= 3 && !text.empty())
    {
        snprintf(buf, sizeof(buf), "%s", text.c_str());
    }
    else if(id[0] > 0)
    {
        // 2.55 alpha 86 wrote 56 as its second number
        int minor = (id[0] == 2 && id[1] == 56 && id[4] == 86) ? 55 : id[1];
        size_t n = snprintf(buf, sizeof(buf), "%d.%d.%d", id[0], minor, id[2]);

        if(id[3] > 0)
            n += snprintf(buf + n, sizeof(buf) - n, ".%d", id[3]);

        const char *stage = id[7] ? NULL : id[6] ? "Gamma" : id[5] ? "Beta" : id[4] ? "Alpha" : NULL;
        int stage_number = id[6] ? id[6] : id[5] ? id[5] : id[4];

        if(stage && stage_number > 0)
            n += snprintf(buf + n, sizeof(buf) - n, " %s %d", stage, stage_number);
        else if(stage)
            n += snprintf(buf + n, sizeof(buf) - n, " %s", stage);

        if(nightly)
            snprintf(buf + n, sizeof(buf) - n, " Nightly");
    }

    // It comes from the quest file: printable characters only
    size_t out = 0;

    for(const char *p = buf; *p && out < sizeof(q255_quest_built_with) - 1; ++p)
        if(*p >= 0x20 && *p < 0x7F)
            q255_quest_built_with[out++] = *p;

    q255_quest_built_with[out] = 0;
}

// The header's build and version numbers (major, minor, patch; 0 before header v4), for
// load fixes that depend on them.
int q255_header_build = 0;
int q255_header_version[3] = {0, 0, 0};

int q255_read_header(Q255Section &s, byte *map_count, byte *midi_flags)
{
    if(s.keep)
        q255_quest_built_with[0] = 0;

    if(s.version < 3 || s.version > 9)
        return Q255_NOT_HANDLED;

    int version_id[8] = {0};
    bool nightly = false;
    std::string version_text;

    zquestheader h;
    memset(&h, 0, sizeof(h));

    if(s.keep && s.header)
        memcpy(&h, s.header, sizeof(h));

    h.build = s.getc();
    s.read(h.pwd_hash, sizeof(h.pwd_hash));
    h.internal = s.igetw();
    h.quest_number = s.getc();
    // these grew from 9 to 16 bytes in header v8; 2.53 keeps the first 8 characters
    s.read(h.version, sizeof(h.version));
    if(s.version >= 8)
        s.skip(16 - sizeof(h.version));
    h.version[sizeof(h.version) - 1] = 0;
    s.read(h.minver, sizeof(h.minver));
    if(s.version >= 8)
        s.skip(16 - sizeof(h.minver));
    h.minver[sizeof(h.minver) - 1] = 0;
    s.read(h.title, sizeof(h.title));
    h.title[sizeof(h.title) - 1] = 0;
    s.read(h.author, sizeof(h.author));
    h.author[sizeof(h.author) - 1] = 0;
    h.use_keyfile = s.getc();
    h.data_flags[ZQ_TILES] = s.getc();
    s.skip(4);
    h.data_flags[ZQ_CHEATS2] = s.getc();
    s.skip(14);
    s.read(h.templatepath, sizeof(h.templatepath));
    byte maps = s.getc();

    if(s.version >= 4)
    {
        for(int i = 0; i < 8; ++i)
            version_id[i] = (int)s.igetl();


        s.skip(2 + 4);          // build date
        s.skip(3 * 256);        // devsig, compiler name, compiler version
        s.skip(1024);           // product name
        s.skip(1 + 4 * 4 + 2);  // compiler id, compiler version numbers, developer id
        s.skip(1024);           // module name
        s.skip(2 * 256);        // build date and time stamps
    }

    if(s.version >= 5)
        s.skip(6);              // build time zone

    if(s.version >= 6)
        s.skip(1);              // external zinfo

    if(s.version >= 7)
        nightly = s.getc() != 0;

    if(s.version >= 9)
        version_text = s.getcstr();

    if(s.failed)
        return qe_invalid;

    if(s.keep)
    {
        if(s.header)
            memcpy(s.header, &h, sizeof(h));

        note_built_with(version_id, nightly, version_text);

        q255_header_build = h.build;
        q255_header_version[0] = version_id[0];
        // 2.55 alpha 86 wrote 56 as its second number
        q255_header_version[1] = (version_id[0] == 2 && version_id[1] == 56 && version_id[4] == 86) ? 55 : version_id[1];
        q255_header_version[2] = version_id[2];

        if(map_count)
            *map_count = maps;

        // this format has no midi flags here: the tunes section sets them
        if(midi_flags)
            memset(midi_flags, 0, MIDIFLAGS_SIZE);
    }

    return 0;
}

int q255_read_rules(Q255Section &s)
{
    if(s.version < 13 || s.version > 18)
        return Q255_NOT_HANDLED;

    size_t total = s.version < 15 ? (size_t)QUESTRULES_SIZE : Q255_RULES_SIZE;

    if(!s.has(total))
    {
        s.failed = true;
        return qe_invalid;
    }

    byte rules[Q255_RULES_SIZE];
    memset(rules, 0, sizeof(rules));
    s.read(rules, total);

    if(s.keep)
    {
        void q30_set_quest_rules(const byte *rules, size_t bytes);
        q30_set_quest_rules(rules, total);
    }

    if(s.version > 17)
    {
        dword compile_setting_version = s.igetl();

        if(compile_setting_version > 0)
        {
            word settings = s.igetw();
            s.skip((size_t)settings * (2 + 4));     // id, value
        }
    }

    if(s.failed)
        return qe_invalid;

    // what readrules does by build, for the one version that reaches this loader
    const int build = s.header ? (int)s.header->build : 0;

    // not stored in the engine, so it applies whichever sections are being kept
    g_old_string_frame = !s.header || build < 50 || get_bit(rules, Q255_QR_STRING_FRAME_OLD_WIDTH_HEIGHT) != 0;

    if(s.keep)
    {
        extern dword q255_compat_rule_version;
        const dword compat = s.version > 16 ? q255_compat_rule_version : 0;
        g_old_subscr = compat < 53 || get_bit(rules, Q255_QR_OLD_SUBSCR) != 0;
        al_trace("2.55 rules: compat version %d, old subscreen order %d\n", (int)compat, g_old_subscr ? 1 : 0);

        if(getenv("ZC_Q255_DUMP_USE"))
        {
            for(int n = 0; n < (int)total * 8; ++n)
                if(get_bit(rules, n))
                    al_trace("use rule %d\n", n);
        }
    }

    if(s.keep)
    {
        memcpy(quest_rules, rules, QUESTRULES_SIZE);

        // From compat rule version 12 the bit means "show the intro" (upstream's qr_GANONINTRO);
        // quests saved before that (Alpha 96-99 builds included) still store 2.53's meaning.
        extern dword q255_compat_rule_version;

        if(s.version > 16 && q255_compat_rule_version >= 12)
            set_bit(quest_rules, qr_NOGANONINTRO, get_bit(quest_rules, qr_NOGANONINTRO) ? 0 : 1);

        if(build < 42)
            set_bit(quest_rules, qr_BITMAPOFFSETFIX, 0);

        // readrules resets these for every quest
        memset(extra_rules, 0, EXTRARULES_SIZE);
        set_bit(extra_rules, er_SHORTDGNWALK, get_bit(rules, Q255_QR_SHORTDGNWALK) ? 1 : 0);
        set_bit(extra_rules, er_BROKENCHARINTDRAWING, get_bit(rules, Q255_QR_BROKENCHARINTDRAWING) ? 1 : 0);
    }

    return 0;
}

int q255_read_strings(Q255Section &s)
{
    if(s.version < 5 || s.version > 13)
        return Q255_NOT_HANDLED;

    word count = s.igetw();

    if(s.failed)
        return qe_invalid;

    if(s.keep)
    {
        if(count >= msg_strings_size)
        {
            MsgStr *grown = (MsgStr *)_al_sane_realloc(MsgStrings, sizeof(MsgStr) * MAXMSGS);

            if(grown == NULL)
                return qe_nomem;

            MsgStrings = grown;
            memset(MsgStrings, 0, sizeof(MsgStr) * MAXMSGS);
            msg_strings_size = MAXMSGS;
        }

        init_msgstrings(0, msg_strings_size);
    }

    for(int i = 0; i < count && !s.failed; ++i)
    {
        MsgStr t;
        memset(&t, 0, sizeof(t));

        long length = MSGSIZE + 1;

        if(s.version >= 9)
        {
            length = s.igetl_s();

            if(length < 0 || length > 8193)
                return qe_invalid;
        }

        store_message_text(t, s.getstr((size_t)length), s.version >= 11);

        t.nextstring = s.igetw();

        long tile = s.version >= 6 ? s.igetl_s() : (long)s.igetw();
        q255_note_tile("message", (unsigned long)tile);
        t.tile = (tile < 0 || tile > Q255_LAST_FRAME_TILE) ? 0 : (tile_id)tile;

        t.cset = s.getc();
        t.trans = s.getc() != 0;
        t.font = s.getc();          // numbers past this engine's fonts draw as the default font
        t.x = s.igetw_s();
        t.y = s.igetw_s();
        t.w = s.igetw();
        t.h = s.igetw();

        // this engine draws a frame 16 wider and taller than w and h
        if(!g_old_string_frame)
        {
            t.w = t.w > 16 ? t.w - 16 : 0;
            t.h = t.h > 16 ? t.h - 16 : 0;
        }

        t.hspace = s.getc();
        t.vspace = s.getc();
        t.stringflags = s.getc();

        if(s.version < 13)
            t.stringflags &= ~(0x04 | 0x08);    // defined once, never valid

        if(s.version >= 7)
        {
            byte margins[4];    // up, down, left, right
            s.read(margins, 4);

            if(s.keep)
                q30_set_msg_wrap_extra(i, 16 - (int(margins[2]) + int(margins[3])));

            s.skip(4 + 5);      // portrait tile, portrait cset x y tw th
        }

        if(s.version >= 8)
            s.skip(2);              // shadow type and color

        if(s.version >= 10)
            s.skip(1);              // draw layer

        if(s.version >= 12)
        {
            word sfx = s.igetw();
            t.sfx = sfx > Q255_MAX_SFX_ID ? 0 : (byte)sfx;
        }
        else
            t.sfx = s.getc();

        t.listpos = s.igetw();

        if(s.version >= 13)
        {
            s.skip(3 * (2 + 1 + 2 + 2));    // more, scroll up and scroll down icons
            s.skip(2);                      // active and passive scroll speeds
            s.skip(3 * 2);                  // advance, menu move and menu close sfx
        }

        if(s.keep && !s.failed)
            memcpy(&MsgStrings[i], &t, sizeof(t));
    }

    if(s.failed)
        return qe_invalid;

    if(s.keep)
        msg_count = count;

    return 0;
}

int q255_read_misc(Q255Section &s)
{
    if(s.version < 7 || s.version > 23)
        return Q255_NOT_HANDLED;

    const bool keep = s.keep && s.misc != NULL;

    miscQdata temp;
    memset(&temp, 0, sizeof(temp));

    if(keep)
        memcpy(&temp, s.misc, sizeof(temp));

    memset(temp.shop, 0, sizeof(temp.shop));
    memset(temp.info, 0, sizeof(temp.info));

    // shops
    word shops = s.igetw();

    for(int i = 0; i < shops && !s.failed; ++i)
    {
        shoptype shop;
        memset(&shop, 0, sizeof(shop));
        s.read(shop.name, sizeof(shop.name));

        word items[3];

        for(int j = 0; j < 3; ++j)
            items[j] = s.version >= 20 ? s.igetw() : (word)s.getc();

        for(int j = 0; j < 3; ++j)
            shop.price[j] = s.igetw();

        for(int j = 0; j < 3; ++j)
            shop.hasitem[j] = s.getc();

        for(int j = 0; j < 3; ++j)
        {
            if(items[j] > Q255_MAX_ITEM_ID)
            {
                items[j] = 0;
                shop.hasitem[j] = 0;
            }

            shop.item[j] = (byte)items[j];
        }

        if(i < Q255_MAX_SHOPS)
            temp.shop[i] = shop;
    }

    sort_shop_items(temp);

    // infos
    word infos = s.igetw();

    for(int i = 0; i < infos && !s.failed; ++i)
    {
        infotype info;
        memset(&info, 0, sizeof(info));
        s.read(info.name, sizeof(info.name));

        for(int j = 0; j < 3; ++j)
            info.str[j] = s.igetw();

        for(int j = 0; j < 3; ++j)
            info.price[j] = s.igetw();

        if(i < Q255_MAX_INFOS)
            temp.info[i] = info;
    }

    sort_info_strings(temp);

    // warp rings: some quests store more than the engine has
    word warprings = s.igetw();

    for(int i = 0; i < warprings && !s.failed; ++i)
    {
        const bool store = i < Q255_MAX_WARP_RINGS;

        for(int j = 0; j < 9; ++j)
        {
            word dmap = s.igetw();

            if(store)
                temp.warp[i].dmap[j] = dmap;
        }

        for(int j = 0; j < 9; ++j)
        {
            byte scr = s.getc();

            if(store)
                temp.warp[i].scr[j] = scr;
        }

        byte size = s.getc();

        if(store)
            temp.warp[i].size = size;
    }

    s.read(temp.triforce, sizeof(temp.triforce));
    temp.endstring = s.igetw();

    if(s.version >= 8)
        s.skip((size_t)shops * 3 * 2);      // shop info strings

    if(s.version >= 9)
        s.skip(32 * 4 + 32 * 128);          // quest misc, retired string labels

    if(s.version >= 11)
        s.skip(4);                          // last compiled zscript version

    // misc sprites (falling, drowning, switch poof...), kept for 2.55+ game code
    for(int i = 0; i < 256; ++i)
    {
        int spr = s.version >= 21 ? (int)s.igetw() : s.version >= 12 ? (int)s.getc() : 0;

        if(keep && !s.failed)
            q30_set_misc_sprite(i, spr);
    }

    // bottle types and bottle shops, and the misc sounds bottles play: kept beside QMisc
    // for 2.55+ quests (q30_triggers.cpp)
    if(keep)
        q30_bottles_clear();

    if(s.version >= 13)
    {
        for(int i = 0; i < 64; ++i)
        {
            Q30BottleType bt;
            memset(&bt, 0, sizeof(bt));
            s.read(bt.name, 32);

            for(int j = 0; j < 3; ++j)
            {
                bt.counter[j] = (signed char)s.getc();
                bt.amount[j] = s.igetw();
            }

            bt.flags = s.getc();
            bt.next_type = s.getc();

            if(keep && !s.failed)
                q30_set_bottle_type(i, bt);
        }

        for(int i = 0; i < 256; ++i)
        {
            Q30BottleShop bs;
            memset(&bs, 0, sizeof(bs));
            s.read(bs.name, 32);

            for(int j = 0; j < 3; ++j)
            {
                bs.fill[j] = s.getc();
                bs.comb[j] = s.igetw();
                bs.cset[j] = s.getc();
                bs.price[j] = s.igetw();
                bs.str[j] = s.igetw();
            }

            if(keep && !s.failed)
                q30_set_bottle_shop(i, bs);
        }
    }

    if(s.version >= 14)
    {
        for(int i = 0; i < 256; ++i)
        {
            int msfx = s.version >= 19 ? (int)s.igetw() : (int)s.getc();

            if(keep && !s.failed)
                q30_set_misc_sfx(i, msfx);
        }

        if(keep && s.version < 15)
            q30_set_misc_sfx(8, WAV_MSG);       // sfxREFILL's default before it was in the list
    }

    if(s.version >= 17)
    {
        int ret = skip_save_menus(s);

        if(ret)
            return ret;
    }

    if(s.failed)
        return qe_invalid;

    if(keep)
        memcpy(s.misc, &temp, sizeof(temp));

    return 0;
}

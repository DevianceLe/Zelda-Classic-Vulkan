// A test of the newer-version writers for the init data, misc data and strings on data the user
// changed, for the checks only (nothing happens unless ZQN_EDIT_TEST is set):
//
//   ZQN_EDIT_TEST=1           when a newer-version save first asks one of those writers for its
//                             section, some init data, misc data and strings change first, as
//                             edits in ZQuest's dialogs would;
//   ZQN_EDIT_TEST=<file.qst>  the same, and then the quest is saved in its own version to that
//                             file with the own-format save (as File > Save does) before the
//                             newer save goes on. Upstream's conversion of that file is what a
//                             newer save of the changed quest has to match;
//   ZQN_EDIT_TEST_LOG=<file>  what was changed and how the own-format save went.
//
// The changes can't be made from a load hook: the hooks run before the parts' fingerprints are
// noted, so the own-format save would take the changed data for what was loaded and copy the
// original bytes. Here they come after the load has finished, as a user's would.
//
// Only what the quest's own version can hold is changed (its own-format writers refuse the
// rest), so the edited file carries every change.

#include "precompiled.h" //always first

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "zq_class.h"
#include "zq_writers.h"
#include "zqn_edit_test.h"

extern zinitdata zinit;
extern miscQdata misc;
extern MsgStr *MsgStrings;
extern word msg_count;

namespace
{

FILE *g_log = NULL;

void note(const char *fmt, ...)
{
    if(!g_log)
        return;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
}

// The view the quest was saved at, from right after the load (the fingerprints then move it to
// the editor's view). The own-format save stores the editor's view, as upstream's save does,
// and the editor opens a quest at its own view.
byte g_view_map, g_view_screen;

void note_loaded()
{
    g_view_map = zinit.last_map;
    g_view_screen = zinit.last_screen;
}

ZqLoadHookRegistration g_hook(note_loaded);

word part_version(dword id)
{
    const ZqQuestPart *p = zq_find_part(id);
    word v = 0, cv = 0;

    if(p)
        zq_part_versions(*p, &v, &cv);

    return v;
}

void edit_init(word iv)
{
    zinitdata &z = zinit;
    z.bombs ^= 1;
    z.keys ^= 1;
    z.rupies ^= 4;
    z.triforce ^= 4;
    z.map[0] ^= 2;          // level 1's map, level 2's compass
    z.compass[0] ^= 4;

    // The old rule "continue at full life" is folded into the init data at load (continue at
    // 100%, start hearts = the maximum), and every reader folds it again while the file has it.
    // The own-format save leaves the rule in the file (and a pre-1.93 header can't change it
    // with the body), so the start hearts and continue setting of those quests stay as they are.
    bool contfull = get_bit(z.misc, idM_CONTPERCENT) && z.cont_heart == 100 && z.start_heart == z.hc;

    if(zq_quest_file.zelda_version >= 0x193 && !contfull)
    {
        if(z.start_heart < z.hc)
            ++z.start_heart;
        else if(z.start_heart > 1)
            --z.start_heart;
    }

    note("init: bombs %d, keys %d, rupees %d, triforce 0x%x, start hearts %d", z.bombs, z.keys, z.rupies, z.triforce, z.start_heart);

    if(iv < 18)
        return;

    z.items[20] = !z.items[20];
    z.level_keys[1] = (byte)(z.level_keys[1] + 1);
    z.msg_more_x ^= 8;
    z.msg_more_y = 200;     // past 160, where 3.0's reader stops the "more" icon
    z.msg_more_is_offset ^= 1;
    z.gravity += 2;
    z.terminalv += 50;
    z.jump_link_layer_threshold ^= 8;
    z.ss_grid_x ^= 2;
    z.ss_grid_color ^= 1;
    z.start_dmap ^= 1;
    z.msg_speed ^= 1;
    z.misc[idM_CANSLASH / 8] ^= 1 << (idM_CANSLASH % 8);

    if(!contfull)
    {
        if(get_bit(z.misc, idM_CONTPERCENT))
            z.cont_heart = z.cont_heart == 50 ? 75 : 50;
        else if(z.hc)
            z.cont_heart = (word)(z.cont_heart % z.hc + 1);
    }

    note("init: item 20 %d, level 1 keys %d, more icon %d,%d offset %d, gravity %d, terminal %d, continue %d",
         z.items[20], z.level_keys[1], z.msg_more_x, z.msg_more_y, z.msg_more_is_offset, z.gravity, z.terminalv, z.cont_heart);
}

void edit_misc(word mv)
{
    for(int i = 0; i < 256; ++i)
    {
        if(misc.shop[i].hasitem[0])
        {
            misc.shop[i].price[0] += 5;
            note("misc: shop %d's first price %d", i, misc.shop[i].price[0]);
            break;
        }
    }

    for(int i = 0; i < 256; ++i)
    {
        if(misc.info[i].str[0])
        {
            misc.info[i].price[0] += 3;
            note("misc: info shop %d's first price %d", i, misc.info[i].price[0]);
            break;
        }
    }

    misc.warp[0].scr[0] ^= 1;
    misc.triforce[3] ^= 1;
    note("misc: warp ring 0 place 0 screen %d, triforce piece 3 at %d", misc.warp[0].scr[0], misc.triforce[3]);

    // pre-1.93 quests older than 1.92 build 30 have no end string
    if(zq_quest_file.zelda_version >= 0x193 && msg_count > 2)
    {
        misc.endstring = misc.endstring == 1 ? 2 : 1;
        note("misc: end string %d", misc.endstring);
    }

    if(mv < 7)
        return;

    // the ninth place of a ring, and a shop's name (a name dialog leaves the bytes after the
    // terminator as they were)
    misc.warp[1].scr[8] ^= 2;
    shoptype &s = misc.shop[0];

    if(s.name[0])
        s.name[0] = s.name[0] == 'Z' ? 'Y' : 'Z';
    else
        memcpy(s.name, "Test", 5);

    note("misc: warp ring 1 place 8 screen %d, shop 0 named %.32s", misc.warp[1].scr[8], s.name);
}

size_t text_length(const MsgStr &m)
{
    size_t n = 0;

    while(n <= MSGSIZE && m.s[n])
        ++n;

    return n;
}

void edit_strings(word sv)
{
    if(msg_count < 2)
        return;

    // string 1: a letter's case, or one more letter
    MsgStr &m = MsgStrings[1];
    size_t n = text_length(m);
    bool done = false;

    for(size_t i = 0; i < n && i < 72 && !done; ++i)
    {
        char c = m.s[i];

        if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
        {
            m.s[i] = (char)(c ^ 0x20);
            done = true;
        }
    }

    if(!done && n < 71)
    {
        m.s[n] = 'X';
        m.s[n + 1] = 0;
    }

    if(msg_count > 2)
        m.nextstring = m.nextstring == 2 ? 0 : 2;

    note("strings: string 1 now \"%.40s\", next %d", m.s, m.nextstring);

    if(sv < 5 || zq_quest_file.zelda_version < 0x250)
        return;

    m.x = (short)(m.x <= 200 ? m.x + 8 : m.x - 8);
    m.y ^= 8;
    m.sfx = m.sfx == 18 ? 20 : 18;
    m.stringflags ^= 1;

    // string 2 (or 1): a sound command with an argument past 254 (stored in three bytes), then
    // characters 3.0's text encoding escapes
    MsgStr &t = MsgStrings[msg_count > 2 ? 2 : 1];
    n = text_length(t);
    static const char tail[] = { 21, 47, (char)255, 1, ' ', '\\', '{', 'o', 'k', '}', 0 };

    if(n + sizeof(tail) - 1 <= MSGSIZE)
        memcpy(t.s + n, tail, sizeof(tail));

    note("strings: string 1 at %d,%d, sound %d, flags %d; string %d ends with a sound command",
         m.x, m.y, m.sfx, m.stringflags, msg_count > 2 ? 2 : 1);
}

} // namespace

void zqn_edit_test()
{
    static bool done = false;

    if(done)
        return;

    done = true;
    const char *e = getenv("ZQN_EDIT_TEST");

    if(!e || !*e)
        return;

    const char *log = getenv("ZQN_EDIT_TEST_LOG");
    g_log = log && *log ? fopen(log, "a") : NULL;
    note("quest version 0x%x build %d", zq_quest_file.zelda_version, zq_quest_file.build);

    // quests older than 1.92 build 27 have no init data of their own (the loader sets it)
    word v = zq_quest_file.zelda_version;

    if(v > 0x192 || (v == 0x192 && zq_quest_file.build >= 27))
        edit_init(part_version(ID_INITDATA));
    edit_misc(part_version(ID_MISC));
    edit_strings(part_version(ID_STRINGS));

    if(strcmp(e, "1"))
    {
        Map.setCurrMap(g_view_map);
        Map.setCurrScr(g_view_screen);
        std::string why;
        int r = zq_save_own_format(e, true, why);
        note("own-format save to %s: %s%s", e, r ? "FAILED " : "saved", why.c_str());
    }

    if(g_log)
        fclose(g_log);

    g_log = NULL;
}

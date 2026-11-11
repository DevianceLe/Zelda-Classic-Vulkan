// The edit test of the newer-version writers for subscreens, sounds, tunes and scripts
// (zqn_subscreens.cpp, zqn_media.cpp, zqn_scripts.cpp): those writers take ZQuest's data where
// the user changed it and the original file's bytes elsewhere, and this checks the first path
// end to end against upstream.
//
// ZQN_WG_EDIT=1: the first time one of those writers runs after a load, which is after every
// load snapshot and part fingerprint was taken, the quest's subscreens, sounds, tunes and
// scripts change as edits in ZQuest would, only in ways the quest's own version can hold.
// ZQN_WG_EDIT_SAVE=<file> also saves the edited quest in its own version there (File > Save)
// and writes what was edited and how the save went to <file>.txt. Upstream converting that
// file is then the reference for the newer save of the same edit, made by another run with
// ZQN_WG_EDIT=1 alone (a save in the quest's own version takes new snapshots, after which the
// edits no longer count as changes).

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "zc_alleg.h"
#include "zdefs.h"
#include "zsys.h"
#include "sfx.h"
#include "subscr.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zqn_out.h"

extern SAMPLE               customsfxdata[WAV_COUNT];
extern unsigned char        customsfxflag[WAV_COUNT>>3];
extern char                 *sfx_string[WAV_COUNT];
extern byte                 midi_flags[MIDIFLAGS_SIZE];
extern zctune               *customtunes;
extern ffscript *ffscripts[512];
extern ffscript *itemscripts[256];
extern ffscript *screenscripts[256];
extern ffscript *globalscripts[NUMSCRIPTGLOBAL];
extern std::string zScript;
extern std::map<int, std::pair<std::string, std::string> > ffcmap;
extern std::map<int, std::pair<std::string, std::string> > globalmap;
extern std::map<int, std::pair<std::string, std::string> > itemmap;

namespace
{

std::string g_note;

void note(const char *what, int a = -1, int b = -1)
{
    char buf[160];

    if(b >= 0)
        snprintf(buf, sizeof(buf), "%s %d -> %d; ", what, a, b);
    else if(a >= 0)
        snprintf(buf, sizeof(buf), "%s %d; ", what, a);
    else
        snprintf(buf, sizeof(buf), "%s; ", what);

    g_note += buf;
}

bool has_part(dword id)
{
    return zq_find_part(id) != NULL;
}

word part_version(dword id)
{
    const ZqQuestPart *p = zq_find_part(id);
    word v = 0, cv = 0;

    if(p)
        zq_part_versions(*p, &v, &cv);

    return v;
}

bool flat_quest()
{
    return zq_quest_file.zelda_version < 0x193;
}

// ---- subscreens -----------------------------------------------------------------------------

bool text_type(int t)
{
    return t == ssoTEXT || t == ssoTEXTBOX || t == ssoCURRENTITEMTEXT || t == ssoCURRENTITEMCLASSTEXT;
}

int object_count(const subscreen_group &g)
{
    int n = 0;

    while(n < MAXSUBSCREENITEMS && g.objects[n].type != ssoNULL)
        ++n;

    return n;
}

void edit_subscreens()
{
    if(!has_part(ID_SUBSCREEN))
        return;

    int first = -1, last = -1;

    for(int i = 0; i < MAXCUSTOMSUBSCREENS; ++i)
        if(custom_subscreen[i].objects[0].type != ssoNULL)
        {
            if(first < 0)
                first = i;

            last = i;
        }

    if(first < 0)
        return;

    // the first subscreen: renamed, its first object moved and recoloured
    subscreen_group &g = custom_subscreen[first];
    strcpy(g.name, "Edited subscreen");
    g.objects[0].x = (word)(g.objects[0].x + 3);
    g.objects[0].color1 = (short)(g.objects[0].color1 ^ 1);
    note("subscreen renamed, first object moved", first);
    bool text = false, counter = false, item = false;

    for(int i = first; i <= last; ++i)
        for(int j = 0; j < object_count(custom_subscreen[i]); ++j)
        {
            subscreen_object &o = custom_subscreen[i].objects[j];

            // a text's first letter (in place: the text keeps its buffer)
            if(!text && text_type(o.type) && o.dp1 && ((char *)o.dp1)[0])
            {
                char *t = (char *)o.dp1;
                t[0] = t[0] == 'E' ? 'F' : 'E';
                text = true;
                note("subscreen text changed", i);
            }
            // a counter shows another counter
            else if(!counter && o.type == ssoCOUNTER)
            {
                o.d7 = o.d7 == 0 ? 1 : 0;
                counter = true;
                note("subscreen counter changed", i);
            }
            // an item slot shows another item
            else if(!item && o.type == ssoCURRENTITEM && o.d8 > 0)
            {
                o.d8 = o.d8 % 200 + 1;
                item = true;
                note("subscreen item slot changed", i);
            }
        }

    // the last subscreen loses its last object, unless that is a text (its buffer stays put)
    subscreen_group &l = custom_subscreen[last];
    int n = object_count(l);

    if(n > 1 && !text_type(l.objects[n - 1].type))
    {
        memset(&l.objects[n - 1], 0, sizeof(subscreen_object));
        note("subscreen object removed", last);
    }

    // a copy of the first subscreen, without its texts, after the last
    if(last + 1 < MAXCUSTOMSUBSCREENS)
    {
        subscreen_group &c = custom_subscreen[last + 1];

        for(int j = 0; j < MAXSUBSCREENITEMS; ++j)
            if(c.objects[j].dp1 && text_type(c.objects[j].type))
                return;     // it holds a text's buffer already: leave it alone

        memset(&c, 0, sizeof(subscreen_group));
        c.ss_type = g.ss_type;
        strcpy(c.name, "Added subscreen");
        int k = 0;

        for(int j = 0; j < object_count(g); ++j)
            if(!text_type(g.objects[j].type))
            {
                c.objects[k] = g.objects[j];
                c.objects[k].dp1 = NULL;
                ++k;
            }

        if(k == 0)
            memset(&c, 0, sizeof(subscreen_group));
        else
            note("subscreen added", last + 1);
    }
}

// ---- sounds ---------------------------------------------------------------------------------

bool sfx_flag(int i)
{
    return get_bit(customsfxflag, i - 1) != 0;
}

void edit_sounds()
{
    if(!has_part(ID_SFX))
        return;

    // the slots the quest's own sound section has room for
    int slots = part_version(ID_SFX) < 6 ? 128 : WAV_COUNT;
    std::vector<int> custom;

    for(int i = 1; i < slots; ++i)
        if(sfx_flag(i) && customsfxdata[i].data)
            custom.push_back(i);

    if(custom.empty())
        return;

    // the first is renamed
    int a = custom[0];
    strcpy(sfx_string[a], "Edited sound");
    note("sound renamed", a);

    // the second (or the first) gets another sample rate
    int b = custom.size() > 1 ? custom[1] : custom[0];
    customsfxdata[b].freq += 1000;
    note("sound's rate changed", b);

    // the last moves to the first free slot after it (removed there, added here)
    int c = custom.back();

    for(int m = c + 1; m < slots; ++m)
    {
        if(sfx_flag(m))
            continue;

        SAMPLE t = customsfxdata[c];
        customsfxdata[c] = customsfxdata[m];
        customsfxdata[m] = t;
        char name[36];
        memcpy(name, sfx_string[c], 36);
        name[35] = 0;
        strcpy(sfx_string[m], name);
        set_bit(customsfxflag, c - 1, 0);
        set_bit(customsfxflag, m - 1, 1);
        note("sound moved", c, m);
        break;
    }
}

// ---- tunes ----------------------------------------------------------------------------------

void edit_tunes()
{
    if(!has_part(ID_MIDIS) && !flat_quest())
        return;

    // the tunes the quest's file has room for
    int slots = MAXCUSTOMTUNES;

    if(flat_quest() && (zq_quest_file.zelda_version < 0x192 || (zq_quest_file.zelda_version == 0x192 && zq_quest_file.build < 178)))
        slots = MAXCUSTOMMIDIS192b177;

    std::vector<int> there;

    for(int i = 0; i < slots; ++i)
        if(customtunes[i].data)
            there.push_back(i);

    if(there.empty())
        return;

    // the first: a new title (20 characters at most, which every version holds), volume and loop
    zctune &t = customtunes[there[0]];
    memset(t.title, 0, sizeof(t.title));
    strcpy(t.title, "Edited tune");
    t.volume = t.volume == 200 ? 180 : 200;
    t.loop = t.loop ? 0 : 1;
    t.start = 12;
    t.loop_start = 34;
    t.loop_end = 5678;
    note("tune changed", there[0]);

    // the last moves to the first free slot after it
    int c = there.back();

    for(int m = c + 1; m < slots; ++m)
    {
        if(customtunes[m].data)
            continue;

        zctune tmp;
        memcpy(&tmp, &customtunes[m], sizeof(zctune));
        memcpy(&customtunes[m], &customtunes[c], sizeof(zctune));
        memcpy(&customtunes[c], &tmp, sizeof(zctune));
        tmp.data = NULL;        // its destructor mustn't free what the slots hold
        set_bit(midi_flags, c, 0);
        set_bit(midi_flags, m, 1);
        note("tune moved", c, m);
        break;
    }
}

// ---- scripts --------------------------------------------------------------------------------

bool empty_script(const ffscript *s)
{
    return !s || s[0].command == 0xFFFF;
}

bool jump_command(word c)
{
    // GOTO, GOTOTRUE, GOTOFALSE, GOTOLESS, GOTOMORE, LOOP: their first argument is a command index
    return c == 11 || c == 16 || c == 17 || c == 18 || c == 19 || c == 65;
}

void edit_scripts()
{
    if(!has_part(ID_FFSCRIPT))
        return;

    int first = -1, last = -1;

    for(int i = 0; i < 512; ++i)
        if(!empty_script(ffscripts[i]))
        {
            if(first < 0)
                first = i;

            last = i;
        }

    // a command of the first FFC script: its second argument
    if(first >= 0)
        for(int j = 0; ffscripts[first][j].command != 0xFFFF; ++j)
            if(!jump_command(ffscripts[first][j].command))
            {
                ffscripts[first][j].arg2 ^= 1;
                note("FFC script command changed", first);
                break;
            }

    // the last FFC script (when there are two) is emptied, in place
    if(last > first)
    {
        ffscripts[last][0].command = 0xFFFF;
        note("FFC script emptied", last);
    }

    // the first item script moves to the first empty item slot after it, with its name
    for(int a = 1; a < 256; ++a)
    {
        if(empty_script(itemscripts[a]))
            continue;

        for(int b = a + 1; b < 256; ++b)
            if(empty_script(itemscripts[b]))
            {
                ffscript *t = itemscripts[a];
                itemscripts[a] = itemscripts[b];
                itemscripts[b] = t;

                if(itemmap.count(a) && itemmap[a].second != "")
                {
                    itemmap[b] = itemmap[a];
                    itemmap[a].second = "";
                }

                note("item script moved", a, b);
                break;
            }

        break;
    }

    // a screen script, when there is one, is emptied
    for(int i = 0; i < 256; ++i)
        if(!empty_script(screenscripts[i]))
        {
            screenscripts[i][0].command = 0xFFFF;
            note("screen script emptied", i);
            break;
        }

    // the first global script after ~Init: a command's second argument, and its slot's name
    for(int i = 1; i < NUMSCRIPTGLOBAL; ++i)
    {
        if(empty_script(globalscripts[i]))
            continue;

        for(int j = 0; globalscripts[i][j].command != 0xFFFF; ++j)
            if(!jump_command(globalscripts[i][j].command))
            {
                globalscripts[i][j].arg2 ^= 1;
                note("global script command changed", i);
                break;
            }

        if(globalmap.count(i) && globalmap[i].second != "" && globalmap[i].second[0] != '~')
        {
            globalmap[i].second = "EditedGlobal";
            note("global slot renamed", i);
        }

        break;
    }

    // the source and an FFC slot's name
    zScript += "\n// edited in ZQuest\n";
    note("source changed");

    for(std::map<int, std::pair<std::string, std::string> >::iterator it = ffcmap.begin(); it != ffcmap.end(); ++it)
        if(it->second.second != "")
        {
            it->second.second = "EditedName";
            note("FFC slot renamed", it->first);
            break;
        }
}

} // namespace

void zqn::wg_edit_test_once()
{
    static bool done = false;
    const char *e = getenv("ZQN_WG_EDIT");

    if(done || !e || *e != '1' || zq_quest_file.zelda_version >= 0x255)
        return;

    done = true;
    g_note.clear();
    edit_subscreens();
    edit_sounds();
    edit_tunes();
    edit_scripts();
    al_trace("zqn wG edit test: %s\n", g_note.c_str());
    const char *save = getenv("ZQN_WG_EDIT_SAVE");

    if(!save || !*save)
        return;

    std::string why;
    int r = zq_save_own_format(save, true, why);
    FILE *out = fopen((std::string(save) + ".txt").c_str(), "w");

    if(out)
    {
        fprintf(out, "edited: %s\nown-format save: %s%s%s\n", g_note.c_str(), r ? "FAILED " : "SAVED", r ? ": " : "",
                why.c_str());
        fclose(out);
    }
}

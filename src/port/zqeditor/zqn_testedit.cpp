// Testing the newer-version writers on quests the user changed: environment variables that
// change the open quest right after it loads, as edits in ZQuest's dialogs would, and can save
// it in its own version then. Nothing here runs unless one of them is set.
//
//   ZQN_TEST_RULES="92,-99,+155"   rules flipped (n), cleared (-n) or set (+n)
//   ZQN_TEST_EDIT="title,cheat,drop,door,fav,music"
//                                  the quest's title; cheat code 1; drop set 0 (its first item
//                                  and chance); door combo set 0's first combo; favorite combo
//                                  0; DMap 0's MIDI and enhanced music
//   ZQN_TEST_OWN_SAVE=path         then saves the changed quest in its own version there, with
//                                  ZQuest's save (zq_save_own_format, as File > Save calls it;
//                                  with ZQN_TEST_OWN_SAVE_ALL set, every part through its writer)
//   ZQN_TEST_DUMP_PARTS=folder     every kept part of the opened file, and ZQuest's header, to
//                                  files there (to see bytes the loader changed)
//
// The changes come after every writer area has noted the quest as loaded (this file's hook
// registers a second one when the first load starts, which runs after all the others), so the
// writers see them as the user's. Only the first load of a run is changed.

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "zq_sections.h"
#include "zq_writers.h"

extern zquestheader header;
extern byte quest_rules[QUESTRULES_SIZE];
extern ZCHEATS zcheats;
extern item_drop_object item_drop_sets[MAXITEMDROPSETS];
extern DoorComboSet *DoorComboSets;
extern word door_combo_set_count;
extern int favorite_combos[MAXFAVORITECOMBOS];
extern dmap *DMaps;

namespace
{

bool has_word(const char *list, const char *w)
{
    size_t n = strlen(w);

    for(const char *p = list; (p = strstr(p, w)) != NULL; p += n)
        if((p == list || p[-1] == ',') && (p[n] == 0 || p[n] == ','))
            return true;

    return false;
}

void change_rules(const char *e)
{
    for(const char *p = e; *p;)
    {
        char sign = 0;

        if(*p == '+' || *p == '-')
            sign = *p++;

        char *end;
        long n = strtol(p, &end, 10);

        if(end == p)
        {
            if(*p)
                ++p;

            continue;
        }

        if(n >= 0 && n < QUESTRULES_SIZE * 8)
            set_bit(quest_rules, (int)n, sign == '+' ? 1 : sign == '-' ? 0 : !get_bit(quest_rules, (int)n));

        p = end;
    }
}

void change_data(const char *e)
{
    if(has_word(e, "title"))
    {
        char was[sizeof(header.title)];
        memcpy(was, header.title, sizeof(was));
        was[sizeof(was) - 1] = 0;
        snprintf(header.title, sizeof(header.title), "%.50s (edited)", was);
    }

    if(has_word(e, "cheat"))
        strcpy(zcheats.codes[0], "ZQNTEST");

    if(has_word(e, "drop"))
    {
        item_drop_object &d = item_drop_sets[0];

        if(!d.name[0])
            strcpy(d.name, "Edited set");

        d.item[0] = (word)(d.item[0] == 1 ? 2 : 1);
        d.chance[1] = (word)(d.chance[1] + 3);
    }

    if(has_word(e, "door") && door_combo_set_count > 0)
        DoorComboSets[0].doorcombo_u[0][0] = (word)(DoorComboSets[0].doorcombo_u[0][0] + 1);

    if(has_word(e, "fav"))
        favorite_combos[0] = favorite_combos[0] == 5 ? 6 : 5;

    if(has_word(e, "music"))
    {
        DMaps[0].midi = (byte)(DMaps[0].midi == 4 ? 5 : 4);
        strcpy(DMaps[0].tmusic, "edited.ogg");
    }
}

void change_after_load()
{
    static bool done = false;

    if(done)
        return;

    done = true;
    const char *rules = getenv("ZQN_TEST_RULES"), *edit = getenv("ZQN_TEST_EDIT"), *save = getenv("ZQN_TEST_OWN_SAVE");

    // Saving afterwards: the load's own notes first (every area's copy of the records as
    // loaded, and each part's fingerprint, which this hook would otherwise run before), so the
    // save writes the parts the changes touch and copies the rest, as the user's save does.
    if(save)
        zq_sections_note_loaded();

    if(rules)
        change_rules(rules);

    if(edit)
        change_data(edit);

    al_trace("zqn test: changed after the load (rules %s, data %s)\n", rules ? rules : "-", edit ? edit : "-");

    if(save)
    {
        std::string why;
        int r = getenv("ZQN_TEST_OWN_SAVE_ALL") ? zq_save_own_format_ex(save, true, true, why) : zq_save_own_format(save, true, why);
        al_trace("zqn test: saved in its own version to %s: %d %s\n", save, r, why.c_str());
        std::string rep = std::string(save) + ".txt";
        FILE *f = fopen(rep.c_str(), "w");

        if(f)
        {
            fprintf(f, "%s: %s\n%s\n", save, r ? "NOT SAVED" : "SAVED", why.c_str());
            fclose(f);
        }
    }
}

// ZQN_TEST_DUMP_PARTS=folder: every kept part of the loaded file as it was read, one file each
// (<id>.bin), and ZQuest's header struct (header.bin), to look at bytes the loader changed
void dump_parts()
{
    const char *dir = getenv("ZQN_TEST_DUMP_PARTS");

    if(!dir)
        return;

    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
    {
        const ZqQuestPart &p = zq_quest_file.parts[i];
        std::vector<byte> b((size_t)p.length);

        if(p.length && !zq_part_read(p, 0, &b[0], b.size()))
            continue;

        char name[1100];
        snprintf(name, sizeof(name), "%s/%c%c%c%c_%d.bin", dir, (char)(p.id >> 24) > ' ' ? (char)(p.id >> 24) : '_',
                 (char)(p.id >> 16) > ' ' ? (char)(p.id >> 16) : '_', (char)(p.id >> 8) > ' ' ? (char)(p.id >> 8) : '_',
                 (char)p.id > ' ' ? (char)p.id : '_', (int)i);
        FILE *f = fopen(name, "wb");

        if(f)
        {
            if(!b.empty())
                fwrite(&b[0], 1, b.size(), f);

            fclose(f);
        }
    }

    std::string hn = std::string(dir) + "/header.bin";
    FILE *f = fopen(hn.c_str(), "wb");

    if(f)
    {
        fwrite(&header, 1, sizeof(header), f);
        fclose(f);
    }
}

ZqLoadHookRegistration g_dump_hook(dump_parts);

void register_late_hook()
{
    static bool registered = false;

    if(registered || !(getenv("ZQN_TEST_RULES") || getenv("ZQN_TEST_EDIT") || getenv("ZQN_TEST_OWN_SAVE")))
        return;

    registered = true;
    static ZqLoadHookRegistration late(change_after_load);
}

ZqLoadHookRegistration g_test_hook(register_late_hook);

} // namespace

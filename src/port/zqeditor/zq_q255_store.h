// ZQuest's store for 2.55 and 3.0 Nightly quests (q255_store_zq.cpp): what the section
// readers found that 2.53's structures have no room for. Empty for a 2.53 quest.

#ifndef ZQ_Q255_STORE_H
#define ZQ_Q255_STORE_H

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "zdefs.h"
#include "q30.h"
#include "q30_audio.h"
#include "q30_triggers.h"

struct Zq255Store
{
    enum { GUY_DEFENSES = 41 };

    struct Music
    {
        std::string name, path;
        long loop_start, loop_end, xfade_in, xfade_out;
        byte track;
        short midi;

        Music() : loop_start(0), loop_end(0), xfade_in(0), xfade_out(0), track(0), midi(0) {}
    };

    struct GenericInit
    {
        bool run;
        word exit_state, reload_state;
        dword event_state;
        long initd[8];
        std::map<dword, long> data;
    };

    struct ScreenData
    {
        long size;
        std::map<int, long> values;

        ScreenData() : size(0) {}
    };

    struct SubWidget
    {
        int type;
        std::string label;
        int obj, posflags, x, y, w, h, pos;
        dword genflags, flags;
    };

    struct SubPage
    {
        int cursor_pos;
        std::vector<SubWidget> widgets;

        SubPage() : cursor_pos(0) {}
    };

    struct Subscreen
    {
        int list, slot;             // list: 0 active, 1 passive, 2 overlay, 3 map
        std::string name;
        std::vector<SubPage> pages;
    };

    // combos and rules
    Q30Combo *combos[MAXCOMBOS];
    std::vector<byte> rules;        // the quest's whole rule bitfield

    // items and enemies
    std::map<int, long> item_family;
    std::map<int, int> item_usesound2, item_override_flags;
    std::map<int, std::string> item_display_name;
    std::map<int, bool> item_passive_script;
    std::map<int, Q30ScriptRef> item_sprite_script, item_weapon_script;
    std::map<int, std::vector<byte> > guy_defenses;
    std::map<int, Q30ScriptRef> guy_script, guy_weapon_script;
    std::map<int, dword> guy_moveflags;
    std::map<int, int> guy_wpnsprite;

    // misc and init data
    std::map<int, Q30BottleType> bottle_types;
    std::map<int, Q30BottleShop> bottle_shops;
    std::vector<byte> init_bottle_slots;
    std::map<int, int> misc_sfx, misc_sprites;
    int init_switch_style;
    bool item_box_set;
    int item_box[4];
    bool light_init_set;
    int light_init[6];
    std::map<int, GenericInit> generic_init;
    std::map<int, ScreenData> screen_data_init;

    // maps, strings, DMaps
    std::map<int, byte> placed_flags;   // screen index * 176 + combo position
    std::map<int, int> rooms;           // room types 2.53 has no number for
    std::map<int, int> msg_wrap_extra;
    std::map<int, Q30ScriptRef> dmap_scripts[q30_dmap_kinds];
    std::map<int, std::pair<int, int> > dmap_subscreens;  // overlay, map
    std::map<int, Q30ScriptRef> screen_scripts;

    // music and sound
    std::vector<Music> music;
    std::map<int, std::pair<int, bool> > dmap_music;      // music object, DMap names objects
    std::map<int, long> screen_music;
    std::map<int, std::vector<byte> > ogg_sfx;
    SAMPLE high_sfx[Q30_MAX_SFX - WAV_COUNT];
    std::vector<zctune> high_tunes;

    std::vector<Subscreen> subscreens;

    Zq255Store();
    void clear();
    void clear_sfx();
};

extern Zq255Store zq255;

#include "zq_q255.h"

#endif

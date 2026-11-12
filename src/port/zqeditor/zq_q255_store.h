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
#include "zq_tape.h"

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

    // Records edited in the record editor (zq_record_editor.cpp), whole and in the section's
    // own version, as the writers will put them in: item or enemy id -> bytes. Only the
    // editor and a load change these; the readers don't.
    std::map<int, std::vector<byte> > item_records, guy_records, dmap_records;
    std::vector<byte> init_record;      // the INIT section's data as edited (empty: not edited)
    std::vector<byte> rules_section;    // the RULE section as edited, from its version word on
    // a screen's 2.55/3.0-only fields edited in the screen editor: screen index (map * 136 +
    // screen) -> field key -> the field as edited; zqw_maps.cpp puts them into the record
    std::map<int, std::map<std::string, TapeField> > screen_fields;
    // the MISC section's 2.55/3.0-only fields edited in the misc data editor: key -> the field
    // as edited; zqw_basic.cpp puts them into the section the writer makes
    std::map<std::string, TapeField> misc_fields;
    std::map<std::string, TapeField> string_fields;     // the same for the STR section ("msg12.layer")
    std::map<std::string, TapeField> sub_fields;        // and the SUBS section ("s2.p1.w4.x")

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

// The RULE section of a 2.55 / 3.0 quest taken apart, for the rules editor (zqw_basic.cpp).
struct ZqRuleSection
{
    int version;                // the section's version (13 to 18)
    dword compat;               // its compat rule version (from version 17)
    size_t total;               // the rule bytes the version stores (20; 100 from version 15)
    byte rules[100];            // as stored
    bool has_settings;          // from version 18: the compiler settings
    dword setting_version;
    std::vector<std::pair<word, long> > settings;   // id, value
    std::vector<byte> rest;     // anything after them, kept as it is
};

bool zq255_rules_get(ZqRuleSection &s);     // false: not a 2.55 / 3.0 quest's rules
bool zq255_rules_put(const ZqRuleSection &s, std::string &why);

#include "zq_q255.h"

#endif

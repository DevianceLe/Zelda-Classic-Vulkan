// Things only 2.55 and 3.0 quests have, shared between the player's quest readers
// (q255_*.cpp) and the game code that acts on them. Everything here is a table kept
// beside the 2.53 structures, because those structures' layouts are shared with the
// quest editor and must not change. 2.53 quests leave all of it empty.

#ifndef ZC_Q30_H
#define ZC_Q30_H

#include <map>
#include <string>
#include <vector>

#include "q255.h"

struct Q30ScriptRef
{
    word script;
    long initd[8];

    Q30ScriptRef() : script(0) { memset(initd, 0, sizeof(initd)); }
};

// DMap script slots, as upstream numbers them.
enum { q30_dmap_active, q30_dmap_passive_subscreen, q30_dmap_active_subscreen, q30_dmap_onmap, q30_dmap_kinds };

// q30_core.cpp. Written by the readers; every call is safe with any index.
void q30_clear_tables();
// How much wider a string's text may run than w: 3.0 strings choose their own side margins.
void q30_set_msg_wrap_extra(int str, int extra);
int q30_msg_wrap_extra(int str);
void q30_set_combo_script(int combo, word script, const long *initd);
const Q30ScriptRef *q30_combo_script(int combo);
void q30_set_dmap_script(int dmap, int kind, word script, const long *initd);
const Q30ScriptRef *q30_dmap_script(int dmap, int kind);
// Each dmap's overlay subscreen (an index into the quest's overlay subscreens; upstream's byte,
// 0 by default) and map subscreen (-1 for none, which opens the old map).
void q30_set_dmap_subscreens(int dmap, int overlay, int map);
int q30_dmap_overlay_subscreen(int dmap);
int q30_dmap_map_subscreen(int dmap);
// Each dmap's whole title (2.53 keeps 20 characters) and intro string (upstream's intro_string_id,
// 0 for none), for quests with the newer DMap titles and intros (no qr_OLD_DMAP_INTRO_STRINGS).
void q30_set_dmap_texts(int dmap, const char *title, long intro_string);
void q30_set_dmap_floor(int dmap, int floor);     // upstream's dmap floor (0 when not given)
int q30_dmap_floor(int dmap);
void q30_set_dmap_mirror(int dmap, int mirror);   // upstream's mirrorDMap (-1 when not given)
int q30_dmap_mirror(int dmap);
const char *q30_dmap_title(int dmap);   // NULL when the quest gave none
long q30_dmap_intro_string(int dmap);

// q30_audio.cpp
int q30_read_advmusic(Q255Section &s);

#endif

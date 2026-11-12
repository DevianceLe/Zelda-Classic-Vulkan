// Upstream's names for item classes, counters, weapon types and enemy classes, item class help,
// and its item editor's labels for each class (zq_item_names.cpp, generated from upstream's
// source). Numbers are upstream's, as 2.55 and 3.0 quests store them.

#ifndef ZQ_ITEM_NAMES_H
#define ZQ_ITEM_NAMES_H

#include <string>

// What the item editor calls an item's fields, for its class (empty: the field's own name).
struct Q30ItemLabels
{
    int iclass;
    std::string power, misc[10], flag[16], wpn[10], actionsnd[2];
    std::string h_power, h_misc[10], h_flag[16], h_wpn[10], h_actionsnd[2];
    int misc_input_type[10];    // upstream's nswap* (0 dec, 1 hex, 2 long dec, 3 long hex, 4 bool)

    Q30ItemLabels() : iclass(-1)
    {
        for(int q = 0; q < 10; ++q)
            misc_input_type[q] = 2;
    }
};

// How many item classes / enemy classes the generation has: the usable ones (3.0 Nightly or 2.55.17).
int q30_itemclass_count(bool is30);
int q30_enemyclass_count(bool is30);
int q30_counter_count();

const char *q30_itemclass_name(int type);   // names starting '-' are unused slots
const char *q30_itemclass_help(int type);
const char *q30_counter_name(int counter);  // -1: "(None)"
const char *q30_enemyclass_name(int family);
const char *q30_weapon_type_name(int w);

// Defense outcomes and enemy animation styles: the k-th entry of upstream's list, with its value.
int q30_defense_count();
const char *q30_defense_at(int k, int *value);
int q30_anim_count();
const char *q30_anim_at(int k, int *value);

// The enemy editor's labels for an enemy of this class with these attributes (32 each), and for
// its 16 behaviour flags; empty: the field's own name.
void q30_enemy_labels(int type, const long *attributes, std::string *l_attribute, std::string *h_attribute,
                      std::string *l_bflag, std::string *h_bflag);

// A choice of a dropdown upstream's enemy editor shows for an attribute; lists end with a NULL name.
struct Q30NameVal { const char *name; int value; };

// The attributes (32) upstream's enemy editor shows as dropdowns for an enemy of this class with
// these attributes: the choices, or NULL for a plain number. q30_attr_list_is_enemies: the list
// is the quest's enemies.
void q30_enemy_attr_lists(int type, const long *attributes, const Q30NameVal **lists);
bool q30_attr_list_is_enemies(const Q30NameVal *list);

// The labels for an item of this class with these flags (all 32 bits).
void q30_item_labels(int type, unsigned flags, Q30ItemLabels &out);

#endif

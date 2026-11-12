// Upstream's names for combo types and map flags, and its combo editor's labels for each
// type's attributes and flags (zq_combo_names.cpp, generated from upstream's source). The
// numbers are upstream's, as 2.55 and 3.0 quests store them.

#ifndef ZQ_COMBO_NAMES_H
#define ZQ_COMBO_NAMES_H

#include <string>

struct Q30Combo;

// How many combo types and map flags the generation has (3.0 Nightly, or 2.55.17).
int q30_ctype_count(bool is30);
int q30_mflag_count(bool is30);

const char *q30_ctype_name(int type);   // "" past the end; names starting '-' are unused slots
const char *q30_ctype_help(int type);
const char *q30_mflag_name(int flag);

// Combo trigger flags (upstream's ComboTriggerFlag), with the trigger editor's checkbox text.
int q30_trigflag_count();
const char *q30_trigflag_name(int flag);

// The labels and help for the combo's 24 attributes and 16 flags, which depend on its type and
// sometimes on its flags and attributes. Unnamed ones read "Attributes[n]:" / "Flags[n]" with
// empty help.
void q30_combo_labels(const Q30Combo &x, std::string *l_attribute, std::string *h_attribute, std::string *l_flag,
                      std::string *h_flag);

#endif

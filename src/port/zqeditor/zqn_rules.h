// The quest rules of the open quest as ZC 2.55.17 or 3.0 Nightly holds them after opening it,
// for the writers that save a quest made in 2.53.1 or older as one of those versions.
//
// Upstream's loader starts from the rule bits stored in the file (the rules section, or the
// header of a quest older than 1.93), switches compatibility rules on and off by the quest's
// version in readheader and readrules, and then changes a few more while reading other
// sections and after the load: the strings, combos, init data, scripts and sounds readers,
// the scripted-subscreen check (3.0), the old continue-health rule, and ZQuest's set_rules.
// Readers later in the load test some of those rules as already changed; and some readers
// test the rule bits as stored (deprecated_rules) to convert their own data.
//
// zqn_upstream_rules gives the whole computation in one place, for every section that needs
// a rule's upstream value. Rule numbers are upstream's (zqn_qrs.h); the first 160 are 2.53's
// too.

#ifndef ZQN_RULES_H
#define ZQN_RULES_H

#include "zdefs.h"

#define ZQN_RULES_SIZE 100      // QUESTRULES_NEW_SIZE: 800 rule bits in 2.55 and 3.0

// Everything but loaded describes the quest as it is now: as upstream opens the file a save in
// the quest's own version would write now. When the user changed no rule since the load that is
// the original file. (A save in its own version stores a changed rule by flipping its stored bit,
// or the stored bit 2.53's loader reads it from; upstream then reads that file with the same
// version checks, so a rule upstream sets for this quest's version stays set: see
// overridden_rule.)
struct ZqnRules
{
    // The bits as stored in the file: the 20 bytes the rules section of these versions has,
    // or the header's rule words of a quest older than 1.93, the rest zero.
    byte stored[ZQN_RULES_SIZE];

    // deprecated_rules after readrules: what readers that convert old data test (items, enemies,
    // DMaps, drop sets, subscreens, init data).
    byte deprecated[ZQN_RULES_SIZE];

    // quest_rules right after readrules, before any other reader changed one.
    byte after_readrules[ZQN_RULES_SIZE];

    // quest_rules once upstream's load (and ZQuest's set_rules) is done, for the original file:
    // what its own save of the unchanged quest writes.
    byte loaded[ZQN_RULES_SIZE];

    // the same for the quest as it is now: what upstream's save writes after opening the
    // quest saved in its own version. The rules section writes this.
    byte now[ZQN_RULES_SIZE];

    // the rules section version, header-to-rules compat version and the header the
    // computation used (0 for a quest older than 1.93)
    word s_version;
    dword compat_version;

    // A rule the user changed since the load that upstream's loader sets for a quest of this
    // version whatever the file says (2.55.17 and 3.0 would undo the change), or -1.
    int overridden_rule;

    // readinitdata's fixpolsvoice (init data version below 14, or no sections): the enemy
    // reader then halves Pols Voice's step speed. Not a rule, but loader state the same way.
    bool fix_pols_voice;
};

// Fills r for target ZQ_NEWER_255 or ZQ_NEWER_30 from the open quest (zq_quest_file and
// ZQuest's data). The one place that knows upstream's rule changes at load: readheader,
// readrules (compatibility rules by version and compat-rule version), the strings, combos,
// init data, scripts and sounds readers, the end of loading and ZQuest's set_rules. False when
// the quest isn't one of 2.53.1 or older, or its kept file bytes can't be read.
bool zqn_upstream_rules(int target, ZqnRules &r);

// A rule bit in one of those arrays.
inline bool zqn_rule(const byte *bits, int n)
{
    return ((bits[n >> 3] >> (n & 7)) & 1) != 0;
}

#endif

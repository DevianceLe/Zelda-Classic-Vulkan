// File > New for 2.55.19 and 3.0 Nightly quests, as upstream's own ZQuest makes a new quest from a
// tileset (its TilesetWizard and load_tileset, the same in 2.55.18 and 3.0): once the tileset is
// open in the new quest's version, its rules as upstream's loader has them, then
//   - "Apply bug fixes": the Bugfix rule template (every compatibility rule off, the corrected
//     boomerang animation on, the old string margins fixed);
//   - "Apply script bug fixes": the ZScript Bugfix template;
//   - a ruleset, when one is picked (upstream's Rulesets button: applyRuleset);
//   - 3.0: the viewport deadzone of 32 x 32 that upstream gives new quests (its init_quest).
// The rule lists are upstream's own, by generation (zq_new_rulesets.h, generated from its source
// by tools/zq_gen_new_rulesets.py). The rules are stored as loaded with the generation's latest
// compat rule version, as upstream's editor saves them.
//
// Written for the port (no jwin or Allegro code).

#include "precompiled.h" //always first

#include <allegro.h>

#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_q255_store.h"
#include "zq_rule_names.h"
#include "zq_tape.h"
#include "zq_new_rulesets.h"

extern int q255_header_version[3];
extern zquestheader header;
bool zq255_init_record(std::vector<byte> &rec, int *version);           // zqw_init.cpp
bool zq255_init_commit(const std::vector<byte> &rec, std::string &why);

namespace
{

enum { RULE_BYTES = 100 };

// One generation's lists; rs[] by upstream's ruleset number (1 Authentic NES ... 5 Modern).
struct Gen
{
    const short *compat, *zs_on, *zs_off, *keep, *always, *sub_off, *sub_on;
    const short *rs[6];
    bool rs_templates[6];
    short margins, frame, brang, max;
};

const Gen g255 =
{
    zqr255_COMPAT, zqr255_ZSONRULES, zqr255_ZSOFFRULES, zqr255_KEEP, zqr255_RALWAYS_ON, zqr255_SUBSCR_RULES_OFF,
    zqr255_SUBSCR_RULES_ON,
    { NULL, zqr255_RS_NES, zqr255_RS_FIXEDNES, zqr255_RS_BSZ, zqr255_RS_Z3, zqr255_RS_MODERN },
    { false, zqr255_RS_NES_TEMPLATES, zqr255_RS_FIXEDNES_TEMPLATES, zqr255_RS_BSZ_TEMPLATES, zqr255_RS_Z3_TEMPLATES,
      zqr255_RS_MODERN_TEMPLATES },
    zqr255_OLD_STRING_EDITOR_MARGINS, zqr255_STRING_FRAME_OLD_WIDTH_HEIGHT, zqr255_CORRECTED_EW_BRANG_ANIM, zqr255_MAX
};

const Gen g30 =
{
    zqr30_COMPAT, zqr30_ZSONRULES, zqr30_ZSOFFRULES, zqr30_KEEP, zqr30_RALWAYS_ON, zqr30_SUBSCR_RULES_OFF,
    zqr30_SUBSCR_RULES_ON,
    { NULL, zqr30_RS_NES, zqr30_RS_FIXEDNES, zqr30_RS_BSZ, zqr30_RS_Z3, zqr30_RS_MODERN },
    { false, zqr30_RS_NES_TEMPLATES, zqr30_RS_FIXEDNES_TEMPLATES, zqr30_RS_BSZ_TEMPLATES, zqr30_RS_Z3_TEMPLATES,
      zqr30_RS_MODERN_TEMPLATES },
    zqr30_OLD_STRING_EDITOR_MARGINS, zqr30_STRING_FRAME_OLD_WIDTH_HEIGHT, zqr30_CORRECTED_EW_BRANG_ANIM, zqr30_MAX
};

void put(byte *r, int n, bool on)
{
    if(n < 0 || n >= RULE_BYTES * 8)
        return;

    if(on)
        r[n >> 3] |= (byte)(1 << (n & 7));
    else
        r[n >> 3] &= (byte)~(1 << (n & 7));
}

bool listed(const short *l, int n)
{
    for(; *l >= 0; ++l)
        if(*l == n)
            return true;

    return false;
}

void put_list(byte *r, const short *l, bool on)
{
    for(; *l >= 0; ++l)
        put(r, *l, on);
}

// onStrFix without asking: the old string margins go. The old frame size stays as it is, since
// fixing it changes every string's size (the tilesets upstream ships have it off).
void str_fix(const Gen &g, byte *r)
{
    put(r, g.margins, false);
}

// ruletemplateFixCompat
void fix_compat(const Gen &g, byte *r)
{
    for(const short *l = g.compat; *l >= 0; ++l)
        if(*l != g.margins && *l != g.frame)
            put(r, *l, false);

    put(r, g.brang, true);
    str_fix(g, r);
}

// ruletemplateFixZSCompat
void fix_zs_compat(const Gen &g, byte *r)
{
    put_list(r, g.zs_on, true);
    put_list(r, g.zs_off, false);
}

// applyRuleset
void apply_ruleset(const Gen &g, byte *r, int rs)
{
    for(int i = 0; i < g.max; ++i)
        if(!listed(g.keep, i))
            put(r, i, false);

    str_fix(g, r);
    fix_compat(g, r);
    put_list(r, g.always, true);

    for(const short *l = g.rs[rs]; *l >= 0; l += 2)
        put(r, l[0], l[1] != 0);

    // Modern: the Bugfix and New Subscreen templates
    if(g.rs_templates[rs])
    {
        fix_compat(g, r);
        put_list(r, g.sub_off, false);
        put_list(r, g.sub_on, true);
    }
}

bool set_deadzone(int size, std::string &why)
{
    std::vector<byte> rec;
    int v = 0;

    if(!zq255_init_record(rec, &v))
    {
        why = "its init data can't be edited";
        return false;
    }

    Tape t;
    TapeParser p(rec.empty() ? NULL : &rec[0], rec.size(), 0, &t);

    if(!zq_tape_init(p, v) || p.bad || p.pos != rec.size())
    {
        why = "its init data doesn't read as its version says";
        return false;
    }

    TapeField *w = t.find("vf.deadzone_w"), *h = t.find("vf.deadzone_h");

    if(!w || !h)
    {
        why = "its init data has no viewport settings";
        return false;
    }

    w->value = size;
    h->value = size;
    std::vector<byte> out;
    t.encode(out);
    return out == rec || zq255_init_commit(out, why);
}

} // namespace

// After the tileset was opened as a quest of the new quest's version (2.55 or 3.0): its rules
// with the bug fix templates and ruleset as picked (0: none, else upstream's ruleset number),
// and 3.0's deadzone. why: what failed.
bool zq_new_quest_from_tileset(int ruleset, bool bugfix, bool zs_bugfix, std::string &why)
{
    ZqRuleSection s;

    if(!zq255_rules_get(s) || s.total != RULE_BYTES)
    {
        why = "its rules can't be set this way";
        return false;
    }

    const bool is30 = q255_header_version[0] >= 3;
    const Gen &g = is30 ? g30 : g255;
    byte r[RULE_BYTES];
    memcpy(r, s.rules, RULE_BYTES);

    // as upstream's loader has them (load_tileset clears the rules, then reads the tileset's)
    char title[66], author[66];
    memcpy(title, header.title, 65);
    memcpy(author, header.author, 65);
    title[65] = author[65] = 0;
    ZqRuleQuest q;
    q.zelda_version = header.zelda_version;
    q.build = header.build;
    q.major = q255_header_version[0];
    q.minor = q255_header_version[1];
    q.patch = q255_header_version[2];
    q.title = title;
    q.author = author;
    q.s_version = s.version;
    q.compat = s.compat;
    zq_rules_upstream(is30, r, q);

    if(bugfix)
        fix_compat(g, r);

    if(zs_bugfix)
        fix_zs_compat(g, r);

    if(ruleset >= 1 && ruleset <= 5)
        apply_ruleset(g, r, ruleset);

    memcpy(s.rules, r, RULE_BYTES);

    if(s.version >= 17)
        s.compat = zq_rules_latest_compat(is30);

    if(!zq255_rules_put(s, why))
        return false;

    // upstream's init_quest: "New quests start with a small viewport deadzone"
    if(is30 && !set_deadzone(32, why))
        return false;

    return true;
}

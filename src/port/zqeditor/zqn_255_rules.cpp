// Saving a quest made in 2.55 as 2.55.17 or 3.0 Nightly (zqn_255.h): the quest rules (RULE).
//
// The target's readrules turns rules on or off as it loads an older quest (compatibility rules
// by the header's program version, the quest's compat rule version, a few quests by title):
// zq_rules_upstream, generated from both versions' readrules (zq_rule_names.cpp), does the
// same to the 100 stored bytes. The target then saves them with its own compat rule version
// (65 in 2.55.17, 122 in 3.0), every update decided; 3.0's version 18 adds the compiler settings
// (a settings version of 1, then none: a 2.55 quest has none).

#include "precompiled.h" //always first

#include <string.h>
#include <vector>

#include "zdefs.h"
#include "ffscript.h"
#include "zq_writers.h"
#include "zq_rule_names.h"
#include "zq_q255_store.h"
#include "zqn_255.h"
#include "../game/q30_scripts.h"

extern zquestheader header;
extern int q255_header_version[3];
extern ffscript *ffscripts[NUMSCRIPTFFC];
extern ffscript *itemscripts[NUMSCRIPTITEM];
extern ffscript *guyscripts[NUMSCRIPTGUYS];
extern ffscript *wpnscripts[NUMSCRIPTWEAPONS];
extern ffscript *globalscripts[NUMSCRIPTGLOBAL];
extern ffscript *linkscripts[NUMSCRIPTLINK];
extern ffscript *screenscripts[NUMSCRIPTSCREEN];

namespace
{

// The rules other sections' readers set (both targets), by those sections' versions.
enum
{
    QR_CONTFULL_DEP = 20, QR_FLUCTUATING_ENEMY_JUMP = 316, QR_SPRITE_JUMP_IS_TRUNCATED = 317,
    QR_BUGGY_BUGGY_SLASH_TRIGGERS = 318, QR_HIDE_BOTTOM_8_PIXELS = 482
};

void set_rule(std::vector<byte> &r, int n, bool on)
{
    if(on)
        r[n >> 3] |= (byte)(1 << (n & 7));
    else
        r[n >> 3] &= (byte)~(1 << (n & 7));
}

bool rule(const std::vector<byte> &r, int n)
{
    return (r[n >> 3] >> (n & 7)) & 1;
}

int part_version(dword id)
{
    const ZqQuestPart *p = zq_find_part(id);
    word v = 0, cv = 0;
    return p && zq_part_versions(*p, &v, &cv) ? v : -1;
}

bool script_there(const ffscript *s)
{
    return s && s[0].command != 0xFFFF;
}

// _zasm_for_every_script in compat_qr_hide_bottom_pixels: a SETV or PUSHV of one of these values
bool draws_at_old_bottom(ffscript *const *table, int n)
{
    for(int i = 0; i < n; ++i)
    {
        const ffscript *s = table[i];

        for(int k = 0; s && s[k].command != 0xFFFF; ++k)
        {
            if(s[k].command != SETV && s[k].command != PUSHV)
                continue;

            const long v = s[k].arg2;

            if(v == 167000000 || v == 168000000 || v == 167870000 || v == 167910000 || v == 168130000)
                return true;
        }
    }

    return false;
}

// 3.0's compat_qr_hide_bottom_pixels (qst.cpp): a quest older than 2.55.9 hides the bottom 8
// pixels when a DMap has an active subscreen script, it is one named quest, or a script sets or
// pushes a value that draws near the old bottom of the screen.
bool hide_bottom_pixels(const char *title)
{
    const int *v = q255_header_version;

    if(v[0] > 2 || (v[0] == 2 && (v[1] > 55 || (v[1] == 55 && v[2] >= 9))))
        return false;

    const std::map<int, Q30ScriptRef> &subs = zq255.dmap_scripts[q30_dmap_active_subscreen];

    for(std::map<int, Q30ScriptRef>::const_iterator it = subs.begin(); it != subs.end(); ++it)
    {
        const int sc = it->second.script;

        if(sc > 0 && sc < Q30_DMAP_SLOTS && script_there(q30_dmap_scripts[sc]))
            return true;
    }

    if(!strcmp(title, "Yuurand: Tales of the Labyrinth"))
        return true;

    return draws_at_old_bottom(ffscripts, NUMSCRIPTFFC) || draws_at_old_bottom(itemscripts, NUMSCRIPTITEM) ||
           draws_at_old_bottom(guyscripts, NUMSCRIPTGUYS) || draws_at_old_bottom(wpnscripts, NUMSCRIPTWEAPONS) ||
           draws_at_old_bottom(globalscripts, NUMSCRIPTGLOBAL) || draws_at_old_bottom(linkscripts, NUMSCRIPTLINK) ||
           draws_at_old_bottom(screenscripts, NUMSCRIPTSCREEN) || draws_at_old_bottom(q30_global_scripts, Q30_GLOBAL_SLOTS) ||
           draws_at_old_bottom(q30_hero_scripts, Q30_HERO_SLOTS) || draws_at_old_bottom(q30_lwpn_scripts, Q30_LWPN_SLOTS) ||
           draws_at_old_bottom(q30_ewpn_scripts, Q30_EWPN_SLOTS) || draws_at_old_bottom(q30_dmap_scripts, Q30_DMAP_SLOTS) ||
           draws_at_old_bottom(q30_itemsprite_scripts, Q30_ITEMSPRITE_SLOTS) ||
           draws_at_old_bottom(q30_combo_scripts, Q30_COMBO_SLOTS) || draws_at_old_bottom(q30_generic_scripts, Q30_GENERIC_SLOTS) ||
           draws_at_old_bottom(q30_subscreen_scripts, Q30_SUBSCREEN_SLOTS);
}

// The rules as the target holds them after loading the quest. 0 when they can't be read, 1, or
// 2 when the section isn't one these take (then nothing is set).
int upstream_rules(const ZqNewerContext &ctx, Zqn255Section &s, std::vector<byte> &rules)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version < 15 || ctx.src_version > 17)
        return 2;

    if(!zqn255_section(ctx, s))
        return 0;

    if(s.data.size() != 100)
    {
        zqw_last_error = "its rules aren't laid out as expected";
        return 0;
    }

    const bool t30 = ctx.target == ZQ_NEWER_30;
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
    q.compat = s.extra.size() == 4 ? (unsigned long)(s.extra[0] | (s.extra[1] << 8) | (s.extra[2] << 16) | ((unsigned long)s.extra[3] << 24)) : 0;
    rules = s.data;
    zq_rules_upstream(t30, &rules[0], q);

    // what the script reader (readffscript) and the combo reader set
    const int ffsc = part_version(ID_FFSCRIPT), cmbo = part_version(ID_COMBOS);

    if(ffsc >= 0 && ffsc < 12)
        set_rule(rules, QR_SPRITE_JUMP_IS_TRUNCATED, true);

    if(ffsc >= 0 && ffsc < 19)
        set_rule(rules, QR_FLUCTUATING_ENEMY_JUMP, true);

    if(cmbo >= 13 && cmbo < 21)
        set_rule(rules, QR_BUGGY_BUGGY_SLASH_TRIGGERS, true);

    // after every section: 3.0's check for scripts drawing near the old bottom of the screen
    // (2.55.17's readrules decides that rule by the version alone), then the old continue rule,
    // which the init data takes over
    if(t30 && hide_bottom_pixels(title))
        set_rule(rules, QR_HIDE_BOTTOM_8_PIXELS, true);

    if(rule(rules, QR_CONTFULL_DEP))
        set_rule(rules, QR_CONTFULL_DEP, false);

    return 1;
}

int write_rules(PACKFILE *f, const ZqNewerContext &ctx)
{
    Zqn255Section s;
    std::vector<byte> rules;
    const int rc = upstream_rules(ctx, s, rules);

    if(rc == 2)
        return ZQW_UNSUPPORTED;

    if(rc == 0)
        return 2;

    const bool t30 = ctx.target == ZQ_NEWER_30;
    Zqn255Out compat;
    compat.l((int64_t)zq_rules_latest_compat(t30));

    if(t30)
    {
        Zqn255Out o;
        o.put(rules);
        o.l(1);     // V_COMPILESETTING
        o.w(0);     // no settings
        return zqn255_emit(f, ID_RULES, 18, 18, compat.b, o.b);
    }

    return zqn255_emit(f, ID_RULES, 17, 1, compat.b, rules);
}

ZqNewerRegistration g_reg_rules(ID_RULES, write_rules, "2.55 quest rules");

} // namespace

bool zqn255_rules_loaded(int target, std::vector<byte> &rules)
{
    ZqNewerContext c;
    c.target = target;
    c.src_zelda_version = 0x255;
    c.src_build = 0;
    c.src_part = zq_find_part(ID_RULES);
    c.src_version = c.src_cversion = 0;

    if(c.src_part)
        zq_part_versions(*c.src_part, &c.src_version, &c.src_cversion);

    Zqn255Section s;
    return upstream_rules(c, s, rules) == 1;
}

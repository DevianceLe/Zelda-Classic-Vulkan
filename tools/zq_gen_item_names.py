# Generates src/port/zqeditor/zq_item_names.cpp from upstream's 3.0 source (engine/src):
# item class, counter, weapon and enemy class names, item class help, and the item editor's
# per-class labels (itemeditor.cpp loadinfo). 2.55.17's counts come from its own headers.
#   python -I tools/zq_gen_item_names.py <engine/src> <2.55.17/src> <out.cpp>
import re, sys

eng, u255, out = sys.argv[1], sys.argv[2], sys.argv[3]

def read(p):
    return open(p, encoding='utf-8', errors='replace').read()

def block_after(text, start_pat, open_ch='{', close_ch='}'):
    i = text.index(start_pat)
    j = text.index(open_ch, i)
    depth = 0
    k = j
    while True:
        c = text[k]
        if c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return text[i:k + 1]
        elif c == '"':
            k += 1
            while text[k] != '"':
                if text[k] == '\\':
                    k += 1
                k += 1
        elif text.startswith('//', k):
            k = text.index('\n', k)
            continue
        elif text.startswith('/*', k):
            k = text.index('*/', k) + 1
        k += 1

def enum_containing(text, member):
    k = text.index(member)
    s = text.rindex('enum', 0, k)
    return block_after(text[s:], 'enum')

def count_in(text, member, last):
    body = enum_containing(text, member)
    body = re.sub(r'//[^\n]*', '', body)
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    inner = body[body.index('{') + 1: body.rindex('}')]
    v = -1
    for part in inner.split(','):
        part = part.strip()
        if not part:
            continue
        if '=' in part:
            name, val = [x.strip() for x in part.split('=', 1)]
            v = int(val, 0)
        else:
            name = part
            v += 1
        if name == last:
            return v
    raise SystemExit('no %s' % last)

gen = read(eng + '/base/general.h')
zd = read(eng + '/core/zdefs.h')
itype_enum = enum_containing(gen, 'itype_sword,')
counter_enum = enum_containing(gen, 'crCUSTOM1,')
weapon_enum = enum_containing(zd, 'wNone,wSword')
etype_enum = enum_containing(zd, 'eeGUY=0')
mflag_enum = enum_containing(zd, 'mfEXTENDED, mfMAX')

g255 = read(u255 + '/base/general.h') if False else None
z255 = read(u255 + '/base/zdefs.h')
try:
    n_itype_255 = count_in(z255, 'itype_sword,', 'itype_maxusable')
except Exception:
    n_itype_255 = count_in(read(u255 + '/base/general.h'), 'itype_sword,', 'itype_maxusable')
n_itype_30 = count_in(gen, 'itype_sword,', 'itype_maxusable')
n_etype_30 = count_in(zd, 'eeGUY=0', 'eeMAX')
try:
    n_etype_255 = count_in(z255, 'eeGUY=0', 'eeMAX')
except Exception:
    n_etype_255 = n_etype_30


zl = read(eng + '/zc_list_data.cpp')
ed_enum = enum_containing(zd, 'edNORMAL,')
def pairs(name):
    b = block_after(zl, 'static const GUI::ListData ' + name)
    rows = []
    for line in b.split('\n'):
        t = line.strip()
        if t.startswith('//'):
            continue
        m = re.match(r'\{\s*("[^"]*")\s*,\s*([A-Za-z0-9_]+)\s*\}', t)
        if m:
            rows.append('    { %s, %s }' % (m.group(1), m.group(2)))
    return ',\n'.join(rows), len(rows)
def_rows, n_def = pairs('defense_types')
anim_rows, n_anim = pairs('eanimation_types')
zi = read(eng + '/zinfo.cpp')
itype_names = block_after(zi, 'const char default_itype_strings[itype_max][255]')
itype_help = block_after(zi, 'const char *itemclass_help_string_defaults[itype_max]')
weap_names = block_after(zi, 'const char weap_name_default_string[wMax][255]')
ctr_names = block_after(zi, 'const char counter_default_names[MAX_COUNTERS][255]')
etype_names = block_after(zi, 'const char etype_default_names[eeMAX][255]')

ie = read(eng + '/zq/dialog/itemeditor.cpp')
li = ie.index('void loadinfo(ItemNameInfo * inf, itemdata const& ref)')
sw_at = ie.index('switch(ref.type)', li)
switch = block_after(ie[sw_at:], 'switch')
switch = switch.replace('inf->', 'inf.').replace('ref.type', 'type')
switch = switch.replace('fmt::format(', 'zq_fmt(')
switch = re.sub(r'QRHINT\(\{[^}]*\}\)', 'std::string()', switch)
switch = switch.replace('ZI.getItemClassName(', 'q30_itemclass_name(').replace('ZI.getMapFlagName(', 'q30_mflag_name(')
switch = switch.replace('ZI.getWeapName(', 'weap_name(')
switch = switch.replace('[[fallthrough]];', '// fall through')
assert 'ref.' not in switch, switch[switch.index('ref.') - 100: switch.index('ref.') + 50]
assert 'ZI.' not in switch

# the enemy editor's labels (loadEnemyType): from the default labels to the end of its switch
ee = read(eng + '/zq/dialog/enemyeditor.cpp')
lt = ee.index('void EnemyEditorDialog::loadEnemyType()')
g0 = ee.index('for (int q = 0; q < 32; ++q)', lt)
gsw = ee.index('switch(local_guyref.type)', g0)
gswitch = block_after(ee[gsw:], 'switch')
guy_labels = ee[g0:gsw] + gswitch
guy_labels = guy_labels.replace('local_guyref.type', 'type')
guy_labels = re.sub(r'local_guyref\.attributes\[(\w+)\]', r'attributes[\1]', guy_labels)
guy_labels = guy_labels.replace('fmt::format(', 'zq_fmt(').replace('[[fallthrough]];', '// fall through')
guy_labels = re.sub(r'QRHINT\(\{[^}]*\}\)', 'std::string()', guy_labels)
assert 'local_guyref' not in guy_labels, guy_labels[guy_labels.index('local_guyref') - 80:][:200]
for bad in ('plist_', 'sw_attributes', 'ddl_', 'setText', 'setDisabled'):
    assert bad not in guy_labels, bad

# the enemy editor's attribute dropdowns (loadEnemyType, after its labels) and the labels and
# dropdowns that follow other attributes' values (loadAttributes)
def strip_cmt(t):
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)

attr_lists = []
for m in re.finditer(r'static const GUI::ListData (list_\w+)\s*\{(.*?)\};', strip_cmt(ee[:lt]), re.S):
    rows = re.findall(r'\{\s*("(?:[^"\\]|\\.)*")\s*,\s*(-?\d+)\s*\}', m.group(2))
    attr_lists.append((m.group(1), rows))
attr_list_names = set(n for n, r in attr_lists)

def list_ref(name):
    if name == 'list_enemies':
        return 'g_el_enemies'
    assert name in attr_list_names, name
    return 'g_el_' + name

dd_at = ee.index('#define SW_DROPDOWN 1', lt)
dd_sw = ee.index('switch (local_guyref.type)', dd_at)
dropdowns = block_after(ee[dd_sw:], 'switch')
dropdowns = re.sub(r'ddl_attributes\[(\d+)\]->setListData\((\w+)\);', lambda m: 'lists[%s] = %s;' % (m.group(1), list_ref(m.group(2))), dropdowns)
dropdowns = re.sub(r'\s*sw_attributes\[\d+\]->switchTo\(SW_DROPDOWN\);', '', dropdowns)
dropdowns = dropdowns.replace('local_guyref.type', 'type')
for bad in ('ddl_', 'sw_', 'local_guyref', 'setListData'):
    assert bad not in dropdowns, bad

la = ee.index('void EnemyEditorDialog::loadAttributes()')
la_sw = ee.index('switch(local_guyref.type)', la)
depends = block_after(ee[la_sw:], 'switch')
depends = depends.replace('local_guyref.type', 'type')
depends = re.sub(r'local_guyref\.attributes\[(\w+)\]', r'attributes[\1]', depends)
depends = depends.replace('GUI::ListData const* lists[12] = { nullptr };', 'const Q30NameVal *dl[12] = { NULL };')
# the editor's own widgets: what the loop at the end of each case sets becomes the dropdowns
depends = re.sub(r'for\(auto q : dependent_attribs\)\s*\{\s*if\(lists\[q\]\).*?pbtn_attributes\[q\]->setDisabled\(!plist_attributes\[q\]\);\s*\}',
                 'for(auto q : dependent_attribs) lists[q] = dl[q];', depends, flags=re.S)
depends = re.sub(r'\blists\[(\d+)\] = &(list_\w+);', lambda m: 'dl[%s] = %s;' % (m.group(1), list_ref(m.group(2))), depends)
depends = re.sub(r'static const CheckList \w+\s*=\s*\{.*?\};', '', depends, flags=re.S)
depends = re.sub(r'plist_attributes\[(\d+)\] = &\w+;', r'h_attribute[\1] = "Bits: 1 sword jinx, 2 item jinx, 4 shield jinx.";', depends)
depends = re.sub(r'\s*plist_attributes\[q\] = nullptr;', '', depends)
depends = re.sub(r'QRHINT\(\{[^}]*\}\)', 'std::string()', depends)
depends = depends.replace('fmt::format(', 'zq_fmt(').replace('[[fallthrough]];', '// fall through')
depends = depends.replace('list_animations.findText(', 'anim_text(')
for bad in ('ddl_', 'sw_attributes', 'local_guyref', 'plist_', 'l_attributes[', 'ib_attributes', 'pbtn_', 'GUI::'):
    assert bad not in depends, (bad, depends[depends.index(bad) - 100:][:200])

anim_enum = enum_containing(zd, 'aNONE, aFLIP')
e1_enum = enum_containing(zd, 'e1tNORMAL,')
e2_enum = enum_containing(zd, 'e2tNORMAL,')
e7_enum = enum_containing(zd, 'e7tNORMAL,')
e9_enum = enum_containing(zd, 'e9tNORMAL,')

el_rows = []
for name, rows in attr_lists:
    items = ''.join('    { %s, %s },\n' % (n, v) for n, v in rows)
    el_rows.append('const Q30NameVal g_el_%s[] =\n{\n%s    { NULL, 0 }\n};' % (name, items))
el_rows.append('const Q30NameVal g_el_enemies[] = { { NULL, 0 } };     // the quest\'s enemies')
el_lists = '\n\n'.join(el_rows)

def fix(t, old, new):
    assert old in t, old
    return t.replace(old, new)

itype_names = fix(itype_names, 'const char default_itype_strings[itype_max][255]', 'const char g_itype_names[itype_max][255]')
itype_help = fix(itype_help, 'const char *itemclass_help_string_defaults[itype_max]', 'const char *g_itype_help[itype_max]')
weap_names = fix(weap_names, 'const char weap_name_default_string[wMax][255]', 'const char g_weap_names[wMax][255]')
ctr_names = fix(ctr_names, 'const char counter_default_names[MAX_COUNTERS][255]', 'const char g_ctr_names[MAX_COUNTERS][255]')
etype_names = fix(etype_names, 'const char etype_default_names[eeMAX][255]', 'const char g_etype_names[eeMAX][255]')

src = '''// Generated from upstream ZQuestClassic (3.0 Nightly, commit ef321f8) by tools/zq_gen_item_names.py: item
// class, counter, weapon and enemy class names, item class help, and the item editor's labels
// for each class (itemeditor.cpp loadinfo). Upstream's numbers, which 2.55 and 3.0 quests
// use. Regenerate rather than edit by hand.

#include "precompiled.h" //always first

#include <string>

#include "zq_item_names.h"
#include "zq_combo_names.h"

namespace
{
namespace up30
{
%(itype_enum)s;

%(counter_enum)s;

%(weapon_enum)s;

%(etype_enum)s;

%(mflag_enum)s;

%(ed_enum)s;

enum { nswapDEC, nswapHEX, nswapLDEC, nswapLHEX, nswapBOOL, nswapMAX };

// item_flags (items.h): the generic flags sit between named ones
enum : unsigned
{
    item_flag1 = 1u << 4, item_flag2 = 1u << 5, item_flag3 = 1u << 10, item_flag4 = 1u << 11, item_flag5 = 1u << 12,
    item_flag6 = 1u << 13, item_flag7 = 1u << 14, item_flag8 = 1u << 15, item_flag9 = 1u << 16, item_flag10 = 1u << 17,
    item_flag11 = 1u << 18, item_flag12 = 1u << 19, item_flag13 = 1u << 20, item_flag14 = 1u << 21, item_flag15 = 1u << 22
};

%(itype_names)s;

%(itype_help)s;

%(weap_names)s;

%(ctr_names)s;

%(etype_names)s;

std::string zq_arg(const char *s) { return s ? s : ""; }
std::string zq_arg(const std::string &s) { return s; }
std::string zq_arg(int v) { return std::to_string(v); }

template<typename... A>
std::string zq_fmt(const char *f, A... a)
{
    const std::string args[] = { zq_arg(a)... };
    std::string out;
    size_t n = 0;

    for(const char *p = f; *p; ++p)
    {
        if(p[0] == '{' && p[1] == '}')
        {
            if(n < sizeof(args) / sizeof(args[0]))
                out += args[n++];
            ++p;
        }
        else
            out += *p;
    }

    return out;
}

std::string ordinal(int n)
{
    const char *suf = "th";
    if(n %% 100 < 11 || n %% 100 > 13)
    {
        if(n %% 10 == 1) suf = "st";
        else if(n %% 10 == 2) suf = "nd";
        else if(n %% 10 == 3) suf = "rd";
    }
    return std::to_string(n) + suf;
}

struct NameVal { const char *name; int value; };
const NameVal g_defenses[] =
{
%(def_rows)s
};
const NameVal g_anims[] =
{
%(anim_rows)s
};

const char *weap_name(int w) { return w >= 0 && w < (int)wMax ? g_weap_names[w] : ""; }

void item_labels(int type, unsigned flags, Q30ItemLabels &inf)
{
#define _SET(mem, str, helpstr) do { inf.mem = str; inf.h_##mem = helpstr; } while(false)
#define FLAG(val) (flags & item_flag##val)
#define SWAPTYPE(indx, val) inf.misc_input_type[indx] = val
    std::string classname(q30_itemclass_name(type));
    %(switch)s
#undef _SET
#undef FLAG
#undef SWAPTYPE
}

void guy_labels(int type, const long *attributes, std::string *l_attribute, std::string *h_attribute, std::string *l_bflag,
                std::string *h_bflag)
{
    (void)attributes;
    %(guy_labels)s
}

%(anim_enum)s;

%(e1_enum)s;

%(e2_enum)s;

%(e7_enum)s;

%(e9_enum)s;

%(el_lists)s

std::string anim_text(int v)
{
    for(size_t k = 0; k < sizeof(g_anims) / sizeof(g_anims[0]); ++k)
        if(g_anims[k].value == v)
            return g_anims[k].name;

    return std::string();
}

// loadEnemyType: the attributes shown as dropdowns for the class
void guy_dropdowns(int type, const Q30NameVal **lists)
{
    %(dropdowns)s
}

// loadAttributes: labels and dropdowns that follow other attributes' values
void guy_depends(int type, const long *attributes, std::string *l_attribute, std::string *h_attribute, const Q30NameVal **lists)
{
    %(depends)s
}

} // namespace up30
} // namespace

int q30_itemclass_count(bool is30) { return is30 ? %(n30)d : %(n255)d; }
int q30_enemyclass_count(bool is30) { return is30 ? %(e30)d : %(e255)d; }
int q30_counter_count() { return (int)up30::MAX_COUNTERS; }

const char *q30_itemclass_name(int t)
{
    return t >= 0 && t < (int)up30::itype_max ? up30::g_itype_names[t] : "";
}

const char *q30_itemclass_help(int t)
{
    return t >= 0 && t < (int)up30::itype_max && up30::g_itype_help[t] ? up30::g_itype_help[t] : "";
}

const char *q30_counter_name(int c)
{
    if(c < 0)
        return "(None)";
    return c < (int)up30::MAX_COUNTERS ? up30::g_ctr_names[c] : "";
}

const char *q30_enemyclass_name(int e)
{
    return e >= 0 && e < (int)up30::eeMAX ? up30::g_etype_names[e] : "";
}

const char *q30_weapon_type_name(int w)
{
    return up30::weap_name(w);
}

int q30_defense_count() { return %(n_def)d; }
const char *q30_defense_at(int k, int *value)
{
    if(k < 0 || k >= %(n_def)d) return NULL;
    if(value) *value = up30::g_defenses[k].value;
    return up30::g_defenses[k].name;
}

int q30_anim_count() { return %(n_anim)d; }
const char *q30_anim_at(int k, int *value)
{
    if(k < 0 || k >= %(n_anim)d) return NULL;
    if(value) *value = up30::g_anims[k].value;
    return up30::g_anims[k].name;
}

void q30_enemy_labels(int type, const long *attributes, std::string *l_attribute, std::string *h_attribute, std::string *l_bflag,
                      std::string *h_bflag)
{
    for(int q = 0; q < 32; ++q)
    {
        l_attribute[q].clear();
        h_attribute[q].clear();
    }

    for(int q = 0; q < 16; ++q)
    {
        l_bflag[q].clear();
        h_bflag[q].clear();
    }

    up30::guy_labels(type, attributes, l_attribute, h_attribute, l_bflag, h_bflag);
    const Q30NameVal *lists[32] = { NULL };
    up30::guy_depends(type, attributes, l_attribute, h_attribute, lists);
}

void q30_enemy_attr_lists(int type, const long *attributes, const Q30NameVal **lists)
{
    for(int q = 0; q < 32; ++q)
        lists[q] = NULL;

    up30::guy_dropdowns(type, lists);
    std::string l[32], h[32];
    up30::guy_depends(type, attributes, l, h, lists);
}

bool q30_attr_list_is_enemies(const Q30NameVal *list)
{
    return list == up30::g_el_enemies;
}

void q30_item_labels(int type, unsigned flags, Q30ItemLabels &out)
{
    out = Q30ItemLabels();
    for(int q = 0; q < 10; ++q)
        out.misc_input_type[q] = up30::nswapLDEC;
    up30::item_labels(type, flags, out);
}
''' % dict(itype_enum=itype_enum, counter_enum=counter_enum, weapon_enum=weapon_enum, etype_enum=etype_enum, guy_labels=guy_labels, mflag_enum=mflag_enum, ed_enum=ed_enum, def_rows=def_rows, n_def=n_def, anim_rows=anim_rows, n_anim=n_anim,
           itype_names=itype_names, itype_help=itype_help, anim_enum=anim_enum, e1_enum=e1_enum, e2_enum=e2_enum,
           e7_enum=e7_enum, e9_enum=e9_enum, el_lists=el_lists, dropdowns=dropdowns, depends=depends, weap_names=weap_names, ctr_names=ctr_names,
           etype_names=etype_names, switch=switch, n30=n_itype_30, n255=n_itype_255, e30=n_etype_30, e255=n_etype_255)

open(out, 'w', newline='\n').write(src)
print('item classes 3.0:', n_itype_30, '2.55.17:', n_itype_255, 'enemy classes:', n_etype_30, n_etype_255)

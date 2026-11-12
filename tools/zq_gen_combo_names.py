# Generates src/port/zqeditor/zq_combo_names.cpp from upstream's 3.0 source (engine/src) and
# counts 2.55.17's combo types and map flags.
#   python -I tools/zq_gen_combo_names.py <engine/src> <2.55.17/src> <out.cpp>
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

def enum_names(body):
    body = re.sub(r'//[^\n]*', '', body)
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    inner = body[body.index('{') + 1: body.rindex('}')]
    names = []
    for part in inner.split(','):
        part = part.strip()
        if not part:
            continue
        names.append(part.split('=')[0].strip())
    return names

zd30 = read(eng + '/core/zdefs.h')
i = zd30.index('// combo types')
ctype_enum30 = block_after(zd30, 'enum', ) if False else None
j = zd30.index('enum', i)
ctype_enum30 = block_after(zd30[j:], 'enum')
ct30 = enum_names(ctype_enum30)
assert ct30[-1] == 'cMAX', ct30[-3:]

# map flags: the enum ending in mfMAX
k = zd30.index('mfEXTENDED, mfMAX')
s = zd30.rindex('enum', 0, k)
mf30 = enum_names(block_after(zd30[s:], 'enum'))

zd255 = read(u255 + '/base/zdefs.h')
k = zd255.index('cMAX,')
s = zd255.rindex('enum', 0, k)
ct255 = enum_names(block_after(zd255[s:], 'enum'))
k = zd255.index('mfMAX')
s = zd255.rindex('enum', 0, k)
mf255 = enum_names(block_after(zd255[s:], 'enum'))

zi = read(eng + '/zinfo.cpp')
ctype_names = block_after(zi, 'const char default_ctype_strings[cMAX][255]')
mflag_names = block_after(zi, 'const char map_flag_default_string[mfMAX][255]')

ce = read(eng + '/zq/dialog/comboeditor.cpp')
type_help = block_after(ce, 'static const char *combotype_help_string[cMAX]')
lct = ce.index('void ComboEditorDialog::loadComboType()')
sw_at = ce.index('switch(local_comboref.type) //Label names', lct)
switch = block_after(ce[sw_at:], 'switch')

# adapt the switch to the port
switch = switch.replace('local_comboref.type', 'x.type')
switch = re.sub(r'local_comboref\.c_attributes\[(\w+)\]\.getTrunc\(\)', r'(x.attributes[\1] / 10000)', switch)
switch = switch.replace('NUM_COMBO_ATTRIBUTES', '24')
switch = re.sub(r'QRHINT\(\{[^}]*\}\)', 'std::string()', switch)
switch = switch.replace('get_qr(qr_OLD_SHALLOW_SFX)', 'zq255_rule(50 * 8 + 1)')

switch = switch.replace('fmt::format(', 'zq_fmt(')
switch = switch.replace('[[fallthrough]];', '// fall through')
assert 'local_comboref' not in switch, switch[switch.index('local_comboref') - 200: switch.index('local_comboref') + 100]
assert 'format(' not in switch

# trigger flags: the enum's order, and the trigger editor's checkbox text for each
cmbh = read(eng + '/core/combo.h')
k = cmbh.index('TRIGFLAG_SWORD,')
s0 = cmbh.rindex('enum', 0, k)
trig_enum = enum_names(block_after(cmbh[s0:], 'enum'))
assert trig_enum[-1] == 'TRIGFLAG_MAX', trig_enum[-2:]
cte = read(eng + '/zq/dialog/combo_trigger_editor.cpp')
trig_text = {}
for m in re.finditer(r'TRIGFLAG\((TRIGFLAG_[A-Z0-9_]+),\s*"((?:[^"\\]|\\.)*)"', cte):
    trig_text.setdefault(m.group(1), m.group(2))
def trig_default(n):
    return n[len('TRIGFLAG_'):].replace('_', ' ').title()
trig_rows = ',\n'.join('    "%s"' % trig_text.get(n, trig_default(n)) for n in trig_enum[:-1])
trig_names = 'const char *g_trigflag_names[%d] =\n{\n%s\n}' % (len(trig_enum) - 1, trig_rows)

ctype_names = ctype_names.replace('const char default_ctype_strings[cMAX][255]', 'const char g_ctype_names[up30::cMAX][255]')
mflag_names = mflag_names.replace('const char map_flag_default_string[mfMAX][255]', 'const char g_mflag_names[up30::mfMAX][255]')
type_help = type_help.replace('static const char *combotype_help_string[cMAX]', 'const char *g_ctype_help[up30::cMAX]')

def enum_text(names):
    lines, cur = [], []
    for n in names:
        cur.append(n)
        if len(cur) == 6:
            lines.append('    ' + ', '.join(cur) + ',')
            cur = []
    if cur:
        lines.append('    ' + ', '.join(cur) + ',')
    return '\n'.join(lines)

src = '''// Generated from upstream ZQuestClassic (3.0 Nightly, commit ef321f8) by tools/zq_gen_combo_names.py:
// combo type and map flag names, the combo types' help, and the combo editor's per-type labels
// for attributes and flags (ComboEditorDialog::loadComboType). Upstream's numbers, which
// 2.55 and 3.0 quests use. Regenerate rather than edit by hand.

#include "precompiled.h" //always first

#include <stdarg.h>
#include <stdio.h>
#include <string>

#include "zq_combo_names.h"
#include "zq_q255_store.h"

namespace
{

namespace up30
{
enum
{
%(ct30)s
};

enum
{
%(mf30)s
};

enum { cflag1 = 0x0001, cflag2 = 0x0002, cflag3 = 0x0004, cflag4 = 0x0008, cflag5 = 0x0010, cflag6 = 0x0020,
       cflag7 = 0x0040, cflag8 = 0x0080, cflag9 = 0x0100, cflag10 = 0x0200, cflag11 = 0x0400, cflag12 = 0x0800,
       cflag13 = 0x1000, cflag14 = 0x2000, cflag15 = 0x4000, cflag16 = 0x8000 };
enum { CMBTY_CRUMBLE_RESET, CMBTY_CRUMBLE_CUMULATIVE, CMBTY_CRUMBLE_INEVITABLE };
enum direction { dir_invalid = -1, up, down, left, right, l_up, r_up, l_down, r_down };
const char *dirstr[] = { "up", "down", "left", "right", "up-left", "up-right", "down-left", "down-right" };

%(ctype_names)s;

%(mflag_names)s;

%(type_help)s;

%(trig_names)s;

// fmt::format's "{}" placeholders, filled in order
std::string zq_arg(const char *s) { return s ? s : ""; }
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

bool zq255_rule(int id)
{
    const size_t b = (size_t)id / 8;
    return b < zq255.rules.size() && (zq255.rules[b] & (1 << (id %% 8))) != 0;
}

// Inside up30 so its names hide 2.53's of the same spelling (cSLOPE isn't 2.53's, cNONE is both).
void combo_labels(const Q30Combo &x, std::string *l_attribute, std::string *h_attribute, std::string *l_flag,
                  std::string *h_flag)
{
#define FL(fl) (x.usrflags & (fl))
    for(size_t q = 0; q < 24; ++q)
    {
        l_attribute[q] = "Attributes[" + std::to_string(q) + "]:";
        h_attribute[q].clear();
        if(q > 15) continue;
        l_flag[q] = "Flags[" + std::to_string(q) + "]";
        h_flag[q].clear();
    }
    %(switch)s
#undef FL
}

} // namespace up30
} // namespace

int q30_trigflag_count() { return %(ntrig)d; }

const char *q30_trigflag_name(int f)
{
    return f >= 0 && f < %(ntrig)d ? up30::g_trigflag_names[f] : "";
}

int q30_ctype_count(bool is30) { return is30 ? (int)up30::cMAX : %(n255)d; }
int q30_mflag_count(bool is30) { return is30 ? (int)up30::mfMAX : %(m255)d; }

const char *q30_ctype_name(int t)
{
    return t >= 0 && t < (int)up30::cMAX ? up30::g_ctype_names[t] : "";
}

const char *q30_ctype_help(int t)
{
    return t >= 0 && t < (int)up30::cMAX && up30::g_ctype_help[t] ? up30::g_ctype_help[t] : "";
}

const char *q30_mflag_name(int f)
{
    return f >= 0 && f < (int)up30::mfMAX ? up30::g_mflag_names[f] : "";
}

void q30_combo_labels(const Q30Combo &x, std::string *l_attribute, std::string *h_attribute, std::string *l_flag,
                      std::string *h_flag)
{
    up30::combo_labels(x, l_attribute, h_attribute, l_flag, h_flag);
}
''' % dict(ct30=enum_text(ct30), mf30=enum_text(mf30), ctype_names=ctype_names, mflag_names=mflag_names,
           type_help=type_help, switch=switch, trig_names=trig_names, ntrig=len(trig_enum) - 1, n255=ct255.index('cMAX'), m255=mf255.index('mfMAX'))

open(out, 'w', newline='\n').write(src)
print('types 3.0:', ct30.index('cMAX'), '2.55.17:', ct255.index('cMAX'), 'flags 3.0:', mf30.index('mfMAX'), '2.55.17:', mf255.index('mfMAX'))

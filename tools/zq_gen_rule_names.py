# Generates src/port/zqeditor/zq_rule_names.cpp from upstream's rule dialogs: every quest rule's
# name, help and related rules, by category, as 2.55.17 shows them and as 3.0 Nightly does.
#   python -I tools/zq_gen_rule_names.py <engine/src> <2.55.17/src> <out.cpp>
import re, sys

eng, u255, out = sys.argv[1], sys.argv[2], sys.argv[3]

def read(p):
    return open(p, encoding='utf-8', errors='replace').read()

def strip_comments(t):
    o, k, n = [], 0, len(t)
    while k < n:
        c = t[k]
        if c == '"':
            j = k + 1
            while t[j] != '"':
                j += 2 if t[j] == '\\' else 1
            o.append(t[k:j + 1])
            k = j + 1
        elif t.startswith('//', k):
            k = t.find('\n', k)
            k = n if k < 0 else k
        elif t.startswith('/*', k):
            k = t.index('*/', k) + 2
        else:
            o.append(c)
            k += 1
    return ''.join(o)

def enum_values(text, first, last):
    """name -> value for the enum holding `first`, up to `last`."""
    text = strip_comments(text)
    k = text.index(first)
    s = text.index('{', text.rindex('enum', 0, k))
    e = text.index('}', k)
    v, vals = -1, {}
    for part in text[s + 1:e].split(','):
        part = part.strip()
        if not part:
            continue
        if '=' in part:
            name, val = [x.strip() for x in part.split('=', 1)]
            assert re.match(r'^[A-Za-z0-9_ +\-*()]+$', val), val
            v = int(eval(val, {'__builtins__': {}}, dict(vals)))
        else:
            name = part
            v += 1
        vals[name] = v
        if name == last:
            break
    return vals

TOK = re.compile(r'\s*(?:("(?:[^"\\]|\\.)*")|([A-Za-z_][A-Za-z0-9_:]*)|(\d+)|(.))', re.S)

def tokens(t):
    pos, out = 0, []
    while pos < len(t):
        m = TOK.match(t, pos)
        if not m or m.end() == pos:
            break
        pos = m.end()
        if m.group(1):
            out.append(('s', m.group(1)))
        elif m.group(2):
            out.append(('id', m.group(2)))
        elif m.group(3):
            out.append(('n', m.group(3)))
        elif m.group(4) and not m.group(4).isspace():
            out.append(('p', m.group(4)))
    return out

def unquote(lit):
    return bytes(lit[1:-1], 'utf-8').decode('unicode_escape')

def list_block(text, name):
    m = re.search(r'(?:static\s+)?(?:const\s+)?GUI::ListData\s+' + name + r'\s*\{', text)
    if not m:
        return None
    k = m.end() - 1
    depth = 0
    for j in range(k, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[k + 1:j]
    raise SystemExit('unterminated ' + name)

def entries(body, qrs, where):
    """[(name, rule, help, related)] from a ListData initializer."""
    toks = tokens(body)
    rows, i = [], 0
    while i < len(toks):
        if toks[i] == ('p', ','):
            i += 1
            continue
        if toks[i] != ('p', '{'):
            raise SystemExit('%s: unexpected %r' % (where, toks[i]))
        depth, j = 0, i
        while True:
            if toks[j] == ('p', '{'):
                depth += 1
            elif toks[j] == ('p', '}'):
                depth -= 1
                if depth == 0:
                    break
            j += 1
        inner = toks[i + 1:j]
        i = j + 1
        # split on top-level commas
        fields, cur, d = [], [], 0
        for t in inner:
            if t[0] == 'p' and t[1] in '({':
                d += 1
            elif t[0] == 'p' and t[1] in ')}':
                d -= 1
            if t == ('p', ',') and d == 0:
                fields.append(cur)
                cur = []
            else:
                cur.append(t)
        if cur:
            fields.append(cur)
        name = ''.join(unquote(t[1]) for t in fields[0] if t[0] == 's')
        assert all(t[0] == 's' for t in fields[0]), (where, fields[0])
        assert len(fields[1]) == 1 and fields[1][0][0] == 'id', (where, name, fields[1])
        rule = qrs[fields[1][0][1]]
        help_parts, related = [], []
        if len(fields) > 2:
            f, k = fields[2], 0
            while k < len(f):
                t = f[k]
                if t[0] == 's':
                    help_parts.append(unquote(t[1]))
                    k += 1
                elif t == ('p', '+'):
                    k += 1
                elif t == ('id', 'QRHINT'):
                    e = k
                    while f[e] != ('p', ')'):
                        e += 1
                    related += [qrs[x[1]] for x in f[k:e] if x[0] == 'id' and x[1] != 'QRHINT']
                    k = e + 1
                else:
                    raise SystemExit('%s: %s: unexpected %r in help' % (where, name, t))
        assert len(fields) <= 3, (where, name)
        rows.append((name, rule, ''.join(help_parts), related))
    return rows

def lit(s):
    o = ['"']
    for ch in s:
        if ch == '\\':
            o.append('\\\\')
        elif ch == '"':
            o.append('\\"')
        elif ch == '\n':
            o.append('\\n')
        elif ch == '\t':
            o.append('\\t')
        elif ord(ch) < 32 or ord(ch) > 126:
            o.extend('\\%03o' % b for b in ch.encode('utf-8'))
        else:
            o.append(ch)
    o.append('"')
    return ''.join(o)

LISTS = [
    ('animRulesList', 0), ('comboRulesList', 1), ('compatRulesList', 2), ('enemiesRulesList', 3),
    ('itemRulesList', 4), ('miscRulesList', 5), ('nesfixesRulesList', 6), ('playerRulesList', 7),
    ('subscrRulesList', 8), ('weaponsRulesList', 9), ('scriptRulesList', 10),
    ('instructionRulesList', 11), ('objectRulesList', 12), ('drawingRulesList', 13),
    ('bugfixRulesList', 14), ('compileSettingList', 15),
]

def generation(src, qrs_h, rules_cpp, compile_cpp):
    qrs = enum_values(read(qrs_h), 'qr_SOLIDBLK', 'qr_MAX')
    text = strip_comments(read(rules_cpp))
    ctext = strip_comments(read(compile_cpp))
    tags = re.search(r'std::string tagNames\[[^\]]*\]\s*=\s*\{(.*?)\};', text, re.S).group(1)
    tags = [unquote(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"', tags)][:16]
    rows = []
    for name, cat in LISTS:
        body = list_block(text, name) or list_block(ctext, name)
        assert body is not None, (src, name)
        for r in entries(body, qrs, '%s %s' % (src, name)):
            rows.append((cat,) + r)
    return tags, rows, qrs['qr_MAX']

g255 = generation('2.55.17', u255 + '/base/qrs.h', u255 + '/dialog/quest_rules.cpp',
                  u255 + '/dialog/compilesetting.cpp')
g30 = generation('3.0', eng + '/core/qrs.h', eng + '/dialog/quest_rules.cpp',
                 eng + '/zq/dialog/compilesetting.cpp')

# readrules' updates after the bits are read (by the quest's version and build, its rule
# section version and its compat rule version), as code over a bit array.
def readrules_updates(qrs_h, qst_cpp, zdefs_h):
    qrs = enum_values(read(qrs_h), 'qr_SOLIDBLK', 'qr_MAX')
    ers = enum_values(read(qrs_h), 'er_BITMAPOFFSET', 'er_BITMAPOFFSET')
    t = read(qst_cpp)
    s = t.index('int32_t readrules')
    a = t.index('//Now, do any updates...', s)
    e = t.index('*Header = tempheader;', a)
    body = strip_comments(t[a:e])
    reps = [
        ('std::string(tempheader.title).starts_with(', 'H.title_starts('),
        ('strcmp(tempheader.author,', 'H.author_cmp('),
        ('set_bit(deprecated_rules,', 'R.dep('),
        ('memset(extra_rules, 0, EXTRARULES_SIZE)', 'R.clear_er()'),
        ('set_er(', 'R.set_er('),
        ('set_qr(', 'R.set('),
        ('get_qr(', 'R.get('),
        ('tempheader.compareVer(', 'H.compare('),
        ('tempheader.', 'H.'),
    ]
    for old, new in reps:
        body = body.replace(old, new)
    body = re.sub(r'\bcompatrule_version\b', 'H.compat', body)
    body = re.sub(r'\bs_version\b', 'H.s_version', body)
    body = re.sub(r'\b(qr_[A-Za-z0-9_]+)\b', lambda m: str(qrs[m.group(1)]), body)
    body = re.sub(r'\ber_BITMAPOFFSET\b', str(ers['er_BITMAPOFFSET']), body)
    left = set(re.findall(r'\b[A-Za-z_][A-Za-z0-9_]*\b', re.sub(r'"(?:[^"\\]|\\.)*"', '""', body)))
    allowed = {'H', 'R', 'if', 'else', 'for', 'auto', 'q', 'zelda_version', 'build', 'version_major', 'title_starts',
               'author_cmp', 'dep', 'clear_er', 'set_er', 'set', 'get', 'compare', 'compat', 's_version', 'x190',
               'x192', 'x193', 'x210', 'x211', 'x250', 'x254', 'x255'}
    assert left <= allowed, sorted(left - allowed)
    compat = int(re.search(r'#define V_COMPATRULE\s+(\d+)', read(zdefs_h)).group(1))
    lines = [ln.rstrip() for ln in body.split('\n')]
    lines = [ln for ln in lines if ln.strip()]
    return '\n'.join('    ' + ln.replace('\t', '    ') for ln in lines), compat

u255_code, compat255 = readrules_updates(u255 + '/base/qrs.h', u255 + '/qst.cpp', u255 + '/base/zdefs.h')
u30_code, compat30 = readrules_updates(eng + '/core/qrs.h', eng + '/core/qst_rules.cpp', eng + '/core/zdefs.h')

o = []
o.append('// Generated by tools/zq_gen_rule_names.py from upstream\'s rule dialogs (2.55.17 and 3.0 Nightly).')
o.append('// Do not edit by hand.')
o.append('')
o.append('#include "zq_rule_names.h"')
o.append('')
o.append('#include <string.h>')
o.append('')
o.append('namespace')
o.append('{')
for tag, (tags, rows, qmax) in (('255', g255), ('30', g30)):
    o.append('')
    o.append('const char *const g_cat%s[ZQ_RULE_CATEGORIES] =' % tag)
    o.append('{')
    o.append(',\n'.join('    ' + lit(t) for t in tags))
    o.append('};')
    o.append('')
    o.append('const ZqRuleInfo g_rules%s[] =' % tag)
    o.append('{')
    lines = []
    for cat, name, rule, help_, rel in rows:
        rel = (rel + [-1] * 4)[:4]
        lines.append('    { %d, %d, %s,\n      %s,\n      { %s } }' % (rule, cat, lit(name), lit(help_), ', '.join(map(str, rel))))
    o.append(',\n'.join(lines))
    o.append('};')
o.append('')
o.append('// The rule bits as readrules has them, and its extra rules.')
o.append('struct Bits')
o.append('{')
o.append('    unsigned char *r;')
o.append('    unsigned char er[4];')
o.append('    bool get(int n) const { return (r[n >> 3] >> (n & 7)) & 1; }')
o.append('    void set(int n, int v) { if(v) r[n >> 3] |= (unsigned char)(1 << (n & 7)); else r[n >> 3] &= (unsigned char)~(1 << (n & 7)); }')
o.append('    void dep(int, int) {}       // readrules\' copy of the stored bits (not rules)')
o.append('    void clear_er() { er[0] = er[1] = er[2] = er[3] = 0; }')
o.append('    void set_er(int n, int v) { if(v) er[n >> 3] |= (unsigned char)(1 << (n & 7)); else er[n >> 3] &= (unsigned char)~(1 << (n & 7)); }')
o.append('};')
o.append('')
o.append('struct Head')
o.append('{')
o.append('    const ZqRuleQuest &q;')
o.append('    int zelda_version, build, version_major, compat, s_version;')
o.append('    explicit Head(const ZqRuleQuest &q) : q(q), zelda_version(q.zelda_version), build(q.build), version_major(q.major),')
o.append('                                          compat((int)q.compat), s_version(q.s_version) {}')
o.append('    int compare(int a, int b, int c) const')
o.append('    {')
o.append('        if(q.major != a) return q.major < a ? -1 : 1;')
o.append('        if(q.minor != b) return q.minor < b ? -1 : 1;')
o.append('        if(q.patch != c) return q.patch < c ? -1 : 1;')
o.append('        return 0;')
o.append('    }')
o.append('    bool title_starts(const char *s) const { return strncmp(q.title ? q.title : "", s, strlen(s)) == 0; }')
o.append('    int author_cmp(const char *s) const { return strcmp(q.author ? q.author : "", s); }')
o.append('};')
o.append('')
for tag, code in (('255', u255_code), ('30', u30_code)):
    o.append('void updates%s(Bits &R, const Head &H)' % tag)
    o.append('{')
    o.append(code)
    o.append('}')
    o.append('')
o.append('}')
o.append('')
o.append('void zq_rules_upstream(bool is30, unsigned char *rules, const ZqRuleQuest &q)')
o.append('{')
o.append('    Bits R;')
o.append('    R.r = rules;')
o.append('    R.clear_er();')
o.append('    const Head H(q);')
o.append('')
o.append('    if(is30)')
o.append('        updates30(R, H);')
o.append('    else')
o.append('        updates255(R, H);')
o.append('}')
o.append('')
o.append('unsigned long zq_rules_latest_compat(bool is30)')
o.append('{')
o.append('    return is30 ? %dUL : %dUL;' % (compat30, compat255))
o.append('}')
o.append('')
o.append('int zq_rule_count(bool is30)')
o.append('{')
o.append('    return is30 ? (int)(sizeof(g_rules30) / sizeof(g_rules30[0])) : (int)(sizeof(g_rules255) / sizeof(g_rules255[0]));')
o.append('}')
o.append('')
o.append('const ZqRuleInfo &zq_rule_at(bool is30, int k)')
o.append('{')
o.append('    return is30 ? g_rules30[k] : g_rules255[k];')
o.append('}')
o.append('')
o.append('const char *zq_rule_category(bool is30, int cat)')
o.append('{')
o.append('    if(cat < 0 || cat >= ZQ_RULE_CATEGORIES)')
o.append('        return "??";')
o.append('')
o.append('    return is30 ? g_cat30[cat] : g_cat255[cat];')
o.append('}')
o.append('')
o.append('int zq_rule_max(bool is30)')
o.append('{')
o.append('    return is30 ? %d : %d;' % (g30[2], g255[2]))
o.append('}')
o.append('')
open(out, 'w', newline='\n').write('\n'.join(o))
print('2.55: %d rules, max %d; 3.0: %d rules, max %d' % (len(g255[1]), g255[2], len(g30[1]), g30[2]))

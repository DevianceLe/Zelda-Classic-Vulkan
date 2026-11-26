# Generates src/port/zqeditor/zq_ui_profiles_190.cpp: ZQuest's interface profiles (zq_ui_profile.h) for
# the builds 1.90, 1.92 (Beta 114), 2.10.0-2.10.3 and the 2.11 betas (6, 11-15, 18), read from the old
# ZQuests' own sources in zc-history (git show only; read-only):
#   - menus: the MENU tables of the build's ZQuest (zquest.cc in 1.92; ZQUEST.CPP in 2.10 and 2.11,
#     whose trees keep a stale 1.92 zquest.cc and the player's ZC_SYS.CPP, both with menus of the
#     same names, which aren't read);
#   - each entry's action: today's (2.53.1) menu entry that runs the same editor function, found by
#     the function's name in today's src/zquest.cpp, else the table RENAMED below; an entry the build
#     showed disabled (D_DISABLED), or whose function nothing here has, is shown disabled;
#   - layout and look: written below from those sources (the Small screen 320x240 each build opened
#     in; 1.92's Allegro GUI colours from its zquest.dat palette; 2.10's and 2.11's default jwin
#     colour set, "Windows 2000").
# 1.90 has no source: it gets 1.92 Beta 114's interface. 2.11 Beta 6 has none either (zc-history
# starts at Beta 11): Beta 11's.
#
#   python -I tools/zq_gen_ui_profiles_190.py <zc-history repo> <src/zquest.cpp> <out.cpp>
import re
import subprocess
import sys

hist, today_src, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

# (catalog name, generation, source ref, file, menu group, layout, look)
BUILDS = [
    ('1.90',            0x190, 'origin/1.92b114',   'zquest.cc',  'L192', 'K192'),
    ('1.92 (Beta 114)', 0x192, 'origin/1.92b114',   'zquest.cc',  'L192', 'K192'),
    ('2.10.0',          0x210, 'origin/2.10',       'ZQUEST.CPP', 'L210', 'K210'),
    ('2.10.1',          0x210, 'origin/2.10.1',     'ZQUEST.CPP', 'L210', 'K210'),
    ('2.10.2',          0x210, 'origin/2.10.2',     'ZQUEST.CPP', 'L210', 'K210'),
    ('2.10.3',          0x210, 'origin/2.10.3',     'ZQUEST.CPP', 'L210', 'K210'),
    ('2.11 Beta 6',     0x211, '4260875a',          'ZQUEST.CPP', 'L211', 'K210'),
    ('2.11 Beta 11',    0x211, '4260875a',          'ZQUEST.CPP', 'L211', 'K210'),
    ('2.11 Beta 12',    0x211, 'a1fc3392',          'ZQUEST.CPP', 'L211', 'K210'),
    ('2.11 Beta 13',    0x211, '50b1bc04',          'ZQUEST.CPP', 'L211', 'K210'),
    ('2.11 Beta 14',    0x211, '77172d36',          'ZQUEST.CPP', 'L211', 'K210'),
    ('2.11 Beta 15',    0x211, '5eee8fc8',          'ZQUEST.CPP', 'L211', 'K210'),
    ('2.11 Beta 18',    0x211, 'origin/2.11-Betas', 'ZQUEST.CPP', 'L211', 'K210'),
]

# Old editor functions today's menus call by another name, or not at all ("" = shown disabled).
# Keys: the function, or "function|entry text" where one function sat behind several entries.
RENAMED = {
    'onScrFlags': 'Screen/Screen Data',            # 1.92 Data > Screen Flags: Screen Data's flags
    'onWarp': 'Screen/Tile Warp',                  # 1.92's names for the warp dialogs
    'onWarp2': 'Screen/Side Warp',
    'onEnemyFlags': '@enemy_flags',                # Data > Enemy Flags: Screen Data's enemy flags
    'onRules': 'Quest/Rules/Other',                # one paged "Quest Rules" dialog then
    'onItemProps': 'Quest/Items',                  # 2.10 Misc Data > Item Properties
    'onDefault_Sprites': 'Quest/Defaults/Weapon Sprites',
    'onX': 'Tools/Preview Mode',
    'onEditSFX': 'Quest/Audio/SFX Data',
    'onSaveAs210': 'File/Save as...',              # Save As lists 2.10.x among the builds
    'playTune': 'Etc/The Travels of Link/Lost Woods',
    'playMIDI': 'Etc/Play music',
    'stopMIDI': 'Etc/Stop tunes',
    'onIntegrityCheck|&Warps': 'Tools/Quest Reports/Integrity Check/Warps',
    'onIntegrityCheck|&Rooms': 'Tools/Quest Reports/Integrity Check/Screens',
    'onWhistle': '',                               # whistle warps: gone since 2.50 (warp rings)
    'onComboPage': '',                             # 1.92's combo page picker: no pages since 2.10
    'onEnhancedMusic': '',                         # 2.11's enhanced music dialog (never finished)
}


def git_show(ref, path):
    return subprocess.run(['git', '-C', hist, 'show', '%s:%s' % (ref, path)], capture_output=True,
                          check=True).stdout.decode('latin-1')


def strip_comments(s):
    out = []
    i = 0
    n = len(s)
    while i < n:
        c = s[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and s[j] != c:
                j += 2 if s[j] == '\\' else 1
            out.append(s[i:j + 1])
            i = j + 1
        elif s.startswith('//', i):
            j = s.find('\n', i)
            i = n if j < 0 else j
        elif s.startswith('/*', i):
            j = s.find('*/', i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def split_top(body):
    """Splits at commas outside braces, parentheses and strings."""
    parts = []
    depth = 0
    cur = []
    i = 0
    while i < len(body):
        c = body[i]
        if c == '"':
            j = i + 1
            while body[j] != '"':
                j += 2 if body[j] == '\\' else 1
            cur.append(body[i:j + 1])
            i = j + 1
            continue
        if c in '{(':
            depth += 1
        elif c in '})':
            depth -= 1
        if c == ',' and depth == 0:
            parts.append(''.join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    if ''.join(cur).strip():
        parts.append(''.join(cur).strip())
    return parts


def c_string(e):
    e = re.sub(r'^\(\s*char\s*\*\s*\)\s*', '', e.strip())
    if not e.startswith('"'):
        return None
    pieces = re.findall(r'"((?:[^"\\]|\\.)*)"', e)
    return ''.join(pieces).replace('\\t', '\t').replace('\\"', '"').replace('\\\\', '\\')


def ident(e):
    e = re.sub(r'^\(\s*\w+\s*\*?\s*\)\s*', '', e.strip()).lstrip('&').strip()
    if e in ('NULL', '0', 'nullptr', ''):
        return None
    m = re.match(r'\w+', e)
    return m.group(0) if m else None


def parse_menus(text):
    """{name: [entry]}; entry: None (separator) or (text, proc, child, flags)."""
    s = strip_comments(text)
    s = re.sub(r'(?m)^[ 	]*#.*$', '', s)     # #ifdef branches: both kept, the first entry wins
    menus = {}
    for m in re.finditer(r'\bMENU\s+(\w+)\s*\[\s*\w*\s*\]\s*=\s*\{', s):
        b = m.end() - 1
        depth = 0
        k = b
        while True:
            c = s[k]
            if c == '"':
                k += 1
                while s[k] != '"':
                    k += 2 if s[k] == '\\' else 1
            elif c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0:
                    break
            k += 1
        items = []
        for el in split_top(s[b + 1:k]):
            if not el.startswith('{'):
                continue
            f = split_top(el[1:-1])
            t = c_string(f[0]) if f else None
            if t is None:
                break                       # { NULL }: the end
            if t == '':
                items.append(None)
                continue
            proc = ident(f[1]) if len(f) > 1 else None
            child = ident(f[2]) if len(f) > 2 else None
            flags = f[3].strip() if len(f) > 3 else '0'
            items.append((t, proc, child, flags))
        menus.setdefault(m.group(1), items)  # the first table of a name in the file
    return menus


def audit_text(t):
    t = t.split('\t')[0].replace('&', '')
    return t.rstrip(' ')


# today's entries: editor function -> menu path, as the menu audit writes it
today = parse_menus(open(today_src, encoding='latin-1').read())
today_path = {}


def walk_today(name, prefix):
    for it in today.get(name, []):
        if it is None:
            continue
        t, proc, child, flags = it
        p = prefix + audit_text(t)
        if child:
            walk_today(child, p + '/')
        elif proc:
            today_path.setdefault(proc, p)


walk_today('the_menu', '')
known_paths = set(today_path.values())


def action_for(t, proc, flags, unknown):
    if 'D_DISABLED' in flags:
        return ''
    key = '%s|%s' % (proc, t.split('\t')[0])
    if key in RENAMED:
        return RENAMED[key]
    if proc in RENAMED:
        return RENAMED[proc]
    if proc in today_path:
        return today_path[proc]
    unknown.add(proc)
    return ''


def rows_of(menus, unknown):
    rows = []

    def walk(name, depth):
        for it in menus.get(name, []):
            if it is None:
                rows.append((depth, '', None))
                continue
            t, proc, child, flags = it
            if child:
                rows.append((depth, t, None))
                walk(child, depth + 1)
                continue
            rows.append((depth, t, action_for(t, proc, flags, unknown)))
            # every profile's Etc menu offers the interface choice, after Options
            if depth == 1 and proc == 'onOptions':
                rows.append((depth, '&Interface...', '@interface'))

    for t, proc, child, flags in [it for it in menus['the_menu'] if it]:
        rows.append((0, t, None))
        walk(child, 1)
    return rows


def c_quote(t):
    return '"' + t.replace('\\', '\\\\').replace('"', '\\"').replace('\t', '\\t') + '"'


groups = {}         # rows (as a tuple) -> table name
order = []
build_rows = {}
unknown = set()
for name, gen, ref, path, lay, look in BUILDS:
    rows = tuple(rows_of(parse_menus(git_show(ref, path)), unknown))
    if rows not in groups:
        groups[rows] = 'menu_' + re.sub(r'\W+', '_', name.replace(' (Beta 114)', '')).strip('_').lower()
        order.append(rows)
    build_rows[name] = groups[rows]

for r in order:
    for d, t, a in r:
        if a and not a.startswith('@') and a not in known_paths:
            sys.exit('action %r is no entry of today\'s menus' % a)

o = []
o.append('// zq_ui_profiles_190.cpp: ZQuest\'s interface for quests of 1.90, 1.92 (Beta 114), 2.10.0-2.10.3')
o.append('// and the 2.11 betas (zq_ui_profile.h): each build\'s menus, its Small 320x240 main screen and its')
o.append('// colours, read from its own ZQuest\'s source (zc-history; 1.90 has none: 1.92 Beta 114\'s, as')
o.append('// does 2.11 Beta 6: Beta 11\'s). The 2.11 betas 11 to 15 have profiles of their own for the quests')
o.append('// they made (the catalog names those builds only while such a quest is open, so they carry its')
o.append('// order for them: 20080101 + the beta, as zq_builds_old.cpp\'s dynamic_order).')
o.append('//')
o.append('// GENERATED by tools/zq_gen_ui_profiles_190.py (menus); the layouts and looks are written there.')
o.append('// Written for the port (no jwin or Allegro code): data only.')
o.append('')
o.append('#include "precompiled.h" //always first')
o.append('')
o.append('#include <stddef.h>')
o.append('')
o.append('#include "zq_ui_profile.h"')
o.append('')
o.append('namespace')
o.append('{')
o.append('')
for r in order:
    users = [b for b in build_rows if build_rows[b] == groups[r]]
    o.append('// %s' % ', '.join(users))
    o.append('const zq_ui_menu_row %s[] =' % groups[r])
    o.append('{')
    prev0 = False
    for d, t, a in r:
        if d == 0 and prev0:
            o.append('')
        prev0 = True
        o.append('    { %d, %s, %s, 0 },' % (d, c_quote(t), 'NULL' if a is None else c_quote(a)))
    o.append('};')
    o.append('')

o.append('''// The Small screen (320x240), which every one of these ZQuests opened in (-large gave 2.10 and 2.11
// an 800x600 one). 1.92 and 2.10 put the panels at x 64, 192 wide, and 2.10 its combo list scroller
// at the panel's x+177, y+13; 2.11 has today's Small places (panels at 58, 198 wide).
const zq_ui_rect rects_192[] =
{
    { "panel", -1, 64, ZQ_UI_KEEP, 192, ZQ_UI_KEEP },
    { "combolistscrollers", 0, 241, 205, 11, 11 },
};
const zq_ui_layout layout_192 = { "1.92 Small", 0, 0, 0, rects_192, (int)(sizeof(rects_192) / sizeof(rects_192[0])) };
const zq_ui_layout layout_210 = { "2.10 Small", 0, 0, 0, rects_192, (int)(sizeof(rects_192) / sizeof(rects_192[0])) };
const zq_ui_layout layout_211 = { "2.11 Small", 0, 0, 0, NULL, 0 };

// 1.92: Allegro's own GUI in the editor's VGA colours (zquest.dat's PAL_ZQUEST, entries vc(0..15)):
// yellow vc(14) on blue vc(1), light blue vc(9) for disabled entries, white vc(15) titles, light red
// vc(12) in edit fields, black shadows; the panel's dark blue is dvc(9) (vc(9) halfway to black).
//   dvc 1 black, 2 blue, 3 light blue, 4 yellow, 5 white, 6 light red, 7 the panel's dark blue, 8 cyan
const zq_ui_look look_192 =
{
    "1.92 Allegro GUI", false,
    { 0, 0x000000, 0x0000AA, 0x5555FF, 0xFFFF55, 0xFFFFFF, 0xFF5555, 0x28287D, 0x55FFFF },
    0, 0, 0,
    { 2, 4, 2, 3, 1, 4, 2, 2, 5, 2, 6, 4, 2 },
    1, 1
};

// 2.10 and 2.11: jwin, gui_colorset 0 ("Windows 2000"), the default of every one of them
const zq_ui_look look_210 =
{
    "2.10 Windows 2000", false,
    { 0, 0x000000, 0x424142, 0x848284, 0xD4D0C8, 0xFFFFFF, 0xFFFFE1, 0xFFE1A0, 0x000050 },
    0x0A246A, 0xA6CAF0, 7,
    { 4, 5, 4, 3, 2, 1, 9, 15, 7, 5, 1, 8, 6 },
    1, 1
};
''')

LAYOUT = {'L192': '&layout_192', 'L210': '&layout_210', 'L211': '&layout_211'}
LOOK = {'K192': '&look_192', 'K210': '&look_210'}
o.append('#define ZQ_UI_ROWS(t) t, (int)(sizeof(t) / sizeof(t[0]))')
o.append('')
o.append('const zq_ui_profile profiles[] =')
o.append('{')
for name, gen, ref, path, lay, look in BUILDS:
    # a beta the catalog names only while its quest is open: zq_builds_old.cpp's order for it
    m = re.match(r'2\.11 Beta (\d+)$', name)
    bo = 20080101 + int(m.group(1)) if m and name not in ('2.11 Beta 6', '2.11 Beta 18') else 0
    o.append('    { %s, 0x%x, ZQ_UI_ROWS(%s), %s, %s, NULL, 0, %d },' % (c_quote(name), gen, build_rows[name],
                                                                        LAYOUT[lay], LOOK[look], bo))
o.append('};')
o.append('')
o.append('#undef ZQ_UI_ROWS')
o.append('')
o.append('void add()')
o.append('{')
o.append('    for(int i = 0; i < (int)(sizeof(profiles) / sizeof(profiles[0])); ++i)')
o.append('        zq_ui_add_profile(&profiles[i]);')
o.append('}')
o.append('')
o.append('zq_ui_registrar reg(add);')
o.append('')
o.append('} // namespace')
o.append('')

with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
    f.write('\n'.join(o))

print('%d builds, %d menu tables, %d rows' % (len(BUILDS), len(order), sum(len(r) for r in order)))
if unknown:
    print('shown disabled (no editor function here):', ', '.join(sorted(unknown)))

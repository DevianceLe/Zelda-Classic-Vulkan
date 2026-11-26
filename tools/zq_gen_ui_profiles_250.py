"""Writes the menu tables of src/port/zqeditor/zq_ui_profiles_250_menus.h: ZQuest's menus in 2.50.0,
2.50.1, 2.50.2 and 2.50.3, each entry pointing at the editor function today's (2.53.1) menus run for it.

    python tools/zq_gen_ui_profiles_250.py [menus.json]

The builds' menus come from zquiinv's inventory (H:\\mhfix\\zquiinv\\out\\menus_per_build.json: every
build's the_menu, read from its ZQuest source in H:\\zc-history), today's from src/zquest.cpp as
zqeditor compiles it (ZC_PORT, ZC_ZQEDITOR), parsed with the same reader (H:\\mhfix\\zquiinv\\tools).
An entry runs the action of today's entry that calls the same procedure (its menu path, as the menu
audit writes it); a procedure today's menus don't have gets one of zq_ui_profiles_250.cpp's extra
actions (EXTRA), or none (shown disabled).

2.53.0's menus are 2.53.1's (the same tables in both sources), so its profile keeps today's.
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INV = r'H:\mhfix\zquiinv'
sys.path.insert(0, os.path.join(INV, 'tools'))

BUILDS = ['2.50.0', '2.50.1', '2.50.2', '2.50.3']
# procedures today's menus don't have: the extra action this generation registers for them
EXTRA = {
    'onQuestRules': '@rules250',        # 2.50.2/2.50.3: one tabbed Quest rules window
}


def load_builds(path):
    if path and os.path.exists(path):
        with open(path, encoding='utf-8') as f:
            d = json.load(f)
        return {b: d[b]['root'] for b in BUILDS}
    import zq_ui_src as S
    import zq_ui_menus as M
    out = {}
    for b in S.builds():
        if b['id'] in BUILDS:
            out[b['id']] = M.build_menus(b)['root']
    return out


def today():
    import zq_cparse as C
    import zq_ui_menus as M
    old = set(C.DEFINED)
    C.DEFINED |= {'ZC_PORT', 'ZC_ZQEDITOR'}     # as zqeditor compiles zquest.cpp
    try:
        with open(os.path.join(ROOT, 'src', 'zquest.cpp'), encoding='latin-1') as f:
            menus = M.parse_menus(f.read())
    finally:
        C.DEFINED.clear()
        C.DEFINED |= old
    by = {m['name']: m for m in menus}

    def expand(name, seen):
        out = []
        for it in by[name]['items']:
            it2 = {k: v for k, v in it.items() if k != 'child'}
            if it.get('child') and it['child'] in by and it['child'] not in seen:
                it2['items'] = expand(it['child'], seen | {name})
            out.append(it2)
        return out
    return expand('the_menu', set())


def label(text):
    # the menu audit's path part: no '&', no shortcut, no trailing spaces
    return text.split('\t')[0].replace('&', '').rstrip(' ')


def paths(items, prefix='', out=None):
    """[(path, proc)] of today's entries, in menu order."""
    if out is None:
        out = []
    for it in items:
        if it.get('sep') or 'text' not in it:
            continue
        here = prefix + ('Catch All' if it.get('proc') == 'onCatchall' else label(it['text']))
        if 'items' in it:
            paths(it['items'], here + '/', out)
        elif it.get('proc'):
            out.append((here, it['proc']))
    return out


def c_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\t', '\\t') + '"'


def rows(items, now, depth=0, prefix='', out=None, missing=None):
    if out is None:
        out = []
    byproc = {}
    for p, pr in now:
        byproc.setdefault(pr, []).append(p)
    for it in items:
        if it.get('sep'):
            out.append((depth, '', None))
            continue
        text = it['text'] + ('\t' + it['hotkey'] if it.get('hotkey') else '')
        here = prefix + ('Catch All' if it.get('proc') == 'onCatchall' else label(it['text']))
        if 'items' in it:
            out.append((depth, text, None))
            rows(it['items'], now, depth + 1, here + '/', out, missing)
            continue
        pr = it.get('proc')
        cands = byproc.get(pr, [])
        if here in cands:
            act = here
        elif cands:
            act = cands[0]
        elif pr in EXTRA:
            act = EXTRA[pr]
        else:
            act = ''
            if missing is not None:
                missing.append((here, pr))
        out.append((depth, text, act))
        # Etc > Interface... after Options (unless the build's menus had it)
        if pr == 'onOptions' and not any(x.get('proc') == 'onInterfaceOptions' for x in items):
            out.append((depth, 'Interface...', '@interface'))
    return out


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(INV, 'out', 'menus_per_build.json')
    builds = load_builds(src)
    now = paths(today())
    tables = {}
    for b in BUILDS:
        missing = []
        tables[b] = rows(builds[b], now, missing=missing)
        for m in missing:
            print('%s: no function here for %s (%s)' % (b, m[0], m[1]))
    # builds with the same menus share one table
    names = {}
    order = []
    for b in BUILDS:
        key = json.dumps(tables[b])
        if key not in names:
            names[key] = 'menu_' + b.replace('.', '')
            order.append((names[key], b, [x for x in BUILDS if json.dumps(tables[x]) == key]))
    out = []
    out.append('// zq_ui_profiles_250_menus.h: ZQuest\'s menus in 2.50.0 ... 2.50.3, written by')
    out.append('// tools/zq_gen_ui_profiles_250.py from the builds\' ZQuest sources (H:\\zc-history, through')
    out.append('// zquiinv\'s menu inventory). Each entry\'s action is the path of today\'s (2.53.1) entry that')
    out.append('// calls the same procedure, or an extra action of zq_ui_profiles_250.cpp. Don\'t edit by hand.')
    out.append('#pragma once')
    out.append('')
    for name, b, shared in order:
        out.append('// %s' % ', '.join(shared))
        out.append('const zq_ui_menu_row %s[] =' % name)
        out.append('{')
        for d, t, a in tables[b]:
            out.append('    { %d, %s, %s, 0 },' % (d, c_str(t), 'NULL' if a is None else c_str(a)))
        out.append('};')
        out.append('')
    out.append('// which table each build shows')
    for b in BUILDS:
        out.append('#define ZQ_UI_MENU_%s %s' % (b.replace('.', '_'), names[json.dumps(tables[b])]))
    path = os.path.join(ROOT, 'src', 'port', 'zqeditor', 'zq_ui_profiles_250_menus.h')
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(out) + '\n')
    print('wrote', path, ', '.join('%s %d rows' % (b, len(tables[b])) for b in BUILDS))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Writes src/port/zqeditor/zq_ui_profiles_255.cpp: ZQuest's interface profiles (zq_ui_profile.h)
for the 2.55 releases, 2.55.0 to 2.55.19.

Each release's menus come from zquiinv's menu inventory (every build's menus as its zquest.cpp
built them, read from upstream's source at the release's tag: a JSON file, build -> {root: [...]}).
Every 2.55 entry is mapped onto this ZQuest's editor function: by its procedure, the 2.53.1 menu
entry that runs the same procedure (read from src/zquest.cpp's MENU tables), else the table
PROC_ACTIONS below (2.55 procedures with another name here, or the 2.55 extras of
zq_ui_dialogs_255.h); an entry with no function here is shown disabled with 2.55's text.
Releases with the same menus share one table.

The look is 2.55's default theme (themes/dark.ztheme, gui_colorset 99), the layout the Large one
(2.55's own screen is 912x684: see LAYOUT_NOTE).

Usage: python tools/zq_gen_ui_255.py --menus <zquiinv menus json> [--dialogs <dir>] [--out <cpp>]
"""

import argparse
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

BUILDS = ['2.55.%d' % i for i in range(20)]

# 2.55 procedures this ZQuest runs under another name or menu place, or through an extra action
# (zq_ui_dialogs_255.h). (action, needs)
PROC_ACTIONS = {
    'do_NewQuest': ('File/New', 0),
    'do_OpenQuest': ('File/Open', 0),
    'handle_quit': ('File/Exit', 0),
    'onCmdExit': ('File/Exit', 0),
    'onRulesDlg': ('@qr255_options', 0),
    'onCustomHero': ('Quest/Graphics/Sprites/Link', 0),
    'onStrings': ('@strings255', 0),
    'onBottleTypes': ('@misc255_bottles', 0x255),
    'onBottleShopTypes': ('@misc255_bottle_shops', 0x255),
    'onZInfo': ('Quest/2.55/3.0 ZInfo...', 0x255),
    'onMiscSprites': ('@misc255_sprites', 0x255),
    'onMiscSFX': ('@misc255_sfx', 0x255),
    'onGridToggle': ('View/Show Grid', 0),
    'onShowDarkness': ('@zq255_darkness', 0),
    'onDrawingModePool': ('@pool', 0x255),
    'onDrawingModeAuto': ('@auto', 0x255),
    'onRoom': ('@room255_data', 0),
    'onScrData': ('@screen255_data', 0),
    'onEditSubscreens': ('@subscreens255', 0),
    'onZScriptCompilerSettings': ('@qr255_compiler', 0x255),
    'onZScriptSettings': ('@qr255_script', 0x255),
    'onComboPools': ('Quest/Graphics/Combo Pools...', 0x255),
    'onAutocombos': ('Quest/Graphics/Autocombos...', 0x255),
    'onAutoCombos': ('Quest/Graphics/Autocombos...', 0x255),
    'onEditComboPools': ('Quest/Graphics/Combo Pools...', 0x255),
    'onEditAutoCombos': ('Quest/Graphics/Autocombos...', 0x255),
    'onExport_MsgsText': ('File/Export/Text Dump', 0),
    'do_OpenQuestFile': ('File/Open', 0),
    'onMenuSnapshot': ('Etc/Take Snapshot', 0),
    'onScreenNotes': ('Screen/2.55/3.0 Screen Data...', 0x255),
    # 2.55.12 on: the alias, pool and autocombo pages (lambdas calling call_*_pages)
    'lambda:call_alias_pages': ('Quest/Graphics/Combo Aliases', 0),
    'lambda:call_cpool_pages': ('Quest/Graphics/Combo Pools...', 0x255),
    'lambda:call_autoc_pages': ('Quest/Graphics/Autocombos...', 0x255),
}

# 2.55 procedures with no function in this ZQuest: shown disabled (listed so that a new one is
# noticed: the generator prints procedures that are in neither list).
NO_FUNCTION = set('''
onTileset onImport_StringsTSV onExport_StringsTSV onImport_Tilepack onImport_Tilepack_To
onImport_Combopack onImport_Combopack_To onImport_Comboaliaspack onImport_Comboaliaspack_To
onImport_Doorset onExport_Package onExport_Tilepack onExport_Combopack onExport_Comboaliaspack
onExport_Doorset onTestQst onQMiscValues onRedo onToggleShowFFCs onLayer2BG onLayer3BG
onToggleHighlightLayer onWaterSolidity onEffectFix onPalFix onPitFix onStrFix
onBuggedNextComboLocationReport onImportZASM onZstringshelp do_zq_hotkey_dialog
do_zq_list_hotkeys_dialog toggleConsole onMapscrSnapshot browse_screen_notes onDMapLevelsReport std::bind
'''.split())

LAYOUT_NOTE = ("2.55's screen is 912x684. Its expanded mode (2.53's Large one grown: the map, its page "
               "bar and layer panel at 2.53's places, a 336 wide combo pane with 30 rows a column, favorites "
               "below it) on the Large layout's panels; 2.55's compact mode (the map at 3x) and its fourth combo "
               "column have no place in this editor's drawing")

# 2.55's expanded mode at 912x684 (load_size_poses, is_compact false), on the Large layout's places:
# (what, index, x, y, w, h); None keeps the mode's value. The bottom row (minimap, panels, commands)
# keeps the Large layout's arrangement, grown to the taller screen.
K = None
LAYOUT_RECTS = [
    ('combolist_window', -1, 576, 0, 336, 548),
    ('combo_preview', -1, 704, 6, 32, 32),
    ('combolist', 0, 592, 60, 4, 30),
    ('combolist', 1, 672, 60, 4, 30),
    ('combolist', 2, 752, 60, 4, 30),
    ('comboaliaslist', 0, 592, 60, 4, 25),
    ('comboaliaslist', 1, 672, 60, 4, 25),
    ('comboaliaslist', 2, 752, 60, 4, 25),
    ('comboalias_preview', 0, 592, 476, 64, 64),
    ('comboalias_preview', 1, 672, 476, 64, 64),
    ('comboalias_preview', 2, 752, 476, 64, 64),
    ('combolistscrollers', 0, 613, 47, K, K),
    ('combolistscrollers', 1, 693, 47, K, K),
    ('combolistscrollers', 2, 773, 47, K, K),
    ('favorites_window', -1, 576, 548, 336, 136),
    ('favorites_list', -1, 584, 564, K, K),
    ('panel', -1, K, K, K, 192),
    ('commands_window', -1, K, K, K, 192),
]

# 2.55's default hotkeys (zq_hotkey.cpp's defaults, the same in every 2.55 release) where they run
# something else than today's keys do: (key, action, needs). "": the key does nothing in 2.55.
# Its main screen's own keys are F1 (help), Esc and ' (combos used); every other key is a hotkey.
KEYS = [
    ('F9', '@screen255_data', 0),               # ZQKEY_SCRDATA: Screen Data
    ('R', '@room255_data', 0),                  # ZQKEY_ROOM: Room Data (today: Room Type)
    ('S', '@strings255', 0),                    # ZQKEY_STRINGS: Strings (today: the screen's string)
    ('A', '', 0),                               # today: Catch All
    ('B', '', 0),                               # today: rebuild the translucency table
    ('G', '', 0),                               # today: Guy
    ('H', '', 0),                               # today: flip the combo
    ('Slash', 'Etc/About', 0),                  # ZQKEY_ABOUT (today: the previous flag)
    ('Ctrl+S', 'File/Save', 0),                 # ZQKEY_SAVE's second key
    ('Ctrl+O', 'File/Open', 0),                 # ZQKEY_OPEN's second key
    ('Ctrl+Z', 'Edit/Undo', 0),                 # ZQKEY_UNDO's first key
    ('Shift+V', 'Edit/Paste All', 0),           # ZQKEY_PASTEALL
    ('Ctrl+V', 'Edit/Adv. Paste/Paste To All', 0),          # ZQKEY_PASTETOALL
    ('Ctrl+Shift+V', 'Edit/Adv. Paste/Paste All To All', 0),    # ZQKEY_PASTEALLTOALL
    ('Ctrl+M', 'View/View Map...', 0),          # ZQKEY_VIEW_MAP
]


def c_str(s):
    out = []
    for ch in s:
        if ch == '\\':
            out.append('\\\\')
        elif ch == '"':
            out.append('\\"')
        elif ch == '\t':
            out.append('\\t')
        elif ord(ch) < 32 or ord(ch) > 126:
            out.append('\\x%02X' % ord(ch))
        else:
            out.append(ch)
    return '"' + ''.join(out) + '"'


def audit_text(text):
    t = text.split('\t')[0].replace('&', '')
    return t.rstrip(' ')


def our_menu_paths(zquest_cpp):
    """proc -> path, from zquest.cpp's MENU tables (the paths zq_audit_menu walks)."""
    src = open(zquest_cpp, encoding='latin-1').read()
    tables = {}
    for m in re.finditer(r'(?:static )?MENU (\w+)\[\]\s*=\s*\{(.*?)\n\};', src, re.S):
        rows = []
        for r in re.finditer(r'\{\s*\(char \*\)"((?:[^"\\]|\\.)*)",\s*(\w+),\s*(?:\(MENU \*\)\s*)?(\w+)', m.group(2)):
            rows.append((r.group(1).encode().decode('unicode_escape'), r.group(2), r.group(3)))
        tables[m.group(1)] = rows
    paths = {}
    all_paths = set()

    def walk(name, prefix):
        for text, proc, child in tables.get(name, []):
            here = prefix + ('Catch All' if proc == 'onCatchall' else audit_text(text))
            if child != 'NULL' and child in tables:
                walk(child, here + '/')
            elif proc != 'NULL':
                all_paths.add(here)
                paths.setdefault(proc, here)

    walk('the_menu', '')
    return paths, all_paths


class Gen:
    def __init__(self, paths, all_paths):
        self.paths = paths
        self.all_paths = all_paths
        self.unknown = {}

    def action(self, proc, build):
        if proc in PROC_ACTIONS:
            return PROC_ACTIONS[proc]
        if proc in self.paths:
            return self.paths[proc], 0
        if proc not in NO_FUNCTION:
            self.unknown.setdefault(proc, set()).add(build)
        return '', 0

    def rows(self, items, depth, build, out, under):
        for it in items:
            if it.get('sep'):
                out.append((depth, '', None, 0))
                continue
            if it.get('recent_slot'):
                continue
            text = it.get('text')
            if text is None:
                text = it.get('uid') or '?'
            kids = it.get('items')
            if kids is not None:
                real = [k for k in kids if not k.get('recent_slot')]
                if not real:
                    out.append((depth, text, '', 0))        # Recent: the quests opened lately
                    continue
                out.append((depth, text, None, 0))
                self.rows(kids, depth + 1, build, out, under + [audit_text(text)])
                continue
            proc = it.get('proc')
            if proc and (proc in PROC_ACTIONS or not proc.startswith('lambda')):
                act, needs = self.action(proc, build)
            else:
                act, needs = '', 0
            if act and not act.startswith('@') and act not in self.all_paths:
                print('warning: %s: %s -> %s is not a 2.53.1 menu path' % (build, proc, act), file=sys.stderr)
            out.append((depth, text, act, needs))
            # Etc > Options... is followed by the framework's Interface...
            if proc == 'onOptions' and depth == 1:
                out.append((depth, '&Interface...', '@interface', 0))
        return out


def look_hex(v6):
    """An 8-bit channel that both c*63/255 and c/4 turn back into the 6-bit value v6."""
    return (v6 * 255 + 62) // 63


def dark_look():
    # themes/dark.ztheme (2.55.2 on; 2.55.0 and .1 have the same colours as 6-bit dvc values)
    hexes = [0x1098B8, 0x100800, 0x445050, 0x107090, 0x000000, 0x343838, 0xA8F0C0, 0x18C48C]
    cols = []
    for h in hexes:
        r, g, b = (h >> 16) & 255, (h >> 8) & 255, h & 255
        r, g, b = look_hex(r // 4), look_hex(g // 4), look_hex(b // 4)
        cols.append('0x%02X%02X%02X' % (r, g, b))
    # jcbox jclight jcmedlt jcmeddark jcdark jcboxfg jctitlel jctitler jctitlefg jctextbg jctextfg
    # jcselbg jcselfg
    jc = [6, 5, 6, 3, 2, 1, 3, 5, 7, 5, 1, 8, 6]
    return cols, jc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--menus', required=True)
    ap.add_argument('--dialogs', help="zquiinv's dialog JSON: build -> {dialog name: json} (not used: the "
                    "framework draws every build's dialogs from zquiinv's data itself)")
    ap.add_argument('--zquest', default=os.path.join(ROOT, 'src', 'zquest.cpp'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'src', 'port', 'zqeditor', 'zq_ui_profiles_255.cpp'))
    a = ap.parse_args()

    paths, all_paths = our_menu_paths(a.zquest)
    menus = json.load(open(a.menus, encoding='utf-8'))
    g = Gen(paths, all_paths)

    tables = []         # (name, rows, [builds])
    by_key = {}
    missing = []
    for b in BUILDS:
        m = menus.get(b)
        if not m:
            missing.append(b)
            continue
        rows = g.rows(m['root'], 0, b, [], [])
        key = hashlib.sha1(repr(rows).encode()).hexdigest()
        if key in by_key:
            tables[by_key[key]][2].append(b)
        else:
            by_key[key] = len(tables)
            tables.append(('menu_' + b.replace('.', '_'), rows, [b]))

    dialogs = {}
    if a.dialogs:
        dialogs = json.load(open(a.dialogs, encoding='utf-8'))

    cols, jc = dark_look()
    o = []
    w = o.append
    w('// zq_ui_profiles_255.cpp: ZQuest\'s interface for the 2.55 releases (zq_ui_profile.h), 2.55.0 to')
    w('// 2.55.19: each release\'s menus as its ZQuest had them, on this editor\'s functions, its look (2.55\'s')
    w('// default theme, themes/dark.ztheme) and layout.')
    w('//')
    w('// GENERATED by tools/zq_gen_ui_255.py from zquiinv\'s menu inventory (upstream\'s zquest.cpp menus at')
    w('// each release tag); do not edit by hand. The extra actions 2.55\'s entries need are in')
    w('// zq_ui_dialogs_255.h.')
    w('//')
    w('// Releases with the same menus share a table:')
    for name, rows, bs in tables:
        w('//   %-12s %s' % (name, ', '.join(bs)))
    if missing:
        w('// Not in the inventory (they take the newest profile before them): %s' % ', '.join(missing))
    w('//')
    w('// Written for the port (no jwin or Allegro code).')
    w('')
    w('#include "precompiled.h" //always first')
    w('')
    w('#include "zq_ui_profile.h"')
    w('#include "zq_ui_dialogs_255.h"')
    w('')
    w('namespace')
    w('{')
    w('')
    for name, rows, bs in tables:
        w('// %s' % ', '.join(bs))
        w('const zq_ui_menu_row %s[] =' % name)
        w('{')
        for depth, text, act, needs in rows:
            if depth == 0 and o[-1] != '{':
                w('')
            a_s = 'NULL' if act is None else c_str(act)
            if needs:
                w('    { %d, %s, %s, 0x%X },' % (depth, c_str(text), a_s, needs))
            else:
                w('    { %d, %s, %s, 0 },' % (depth, c_str(text), a_s))
        w('};')
        w('')
    w('// 2.55\'s default theme (gui_colorset 99: themes/dark.ztheme): flat title bars (jctitlel 3, jctitler 5)')
    w('const zq_ui_look look_255 =')
    w('{')
    w('    "2.55 Dark", false,')
    w('    { 0, %s },' % ', '.join(cols))
    w('    0, 0, 0,')
    w('    { %s },' % ', '.join(str(x) for x in jc))
    w('    1, 0')
    w('};')
    w('')
    import textwrap
    for line in textwrap.wrap(LAYOUT_NOTE, 100):
        w('// ' + line)
    w('const zq_ui_rect rects_255[] =')
    w('{')
    for what, idx, x, y, ww, hh in LAYOUT_RECTS:
        f = lambda v: 'ZQ_UI_KEEP' if v is None else str(v)
        w('    { "%s", %d, %s, %s, %s, %s },' % (what, idx, f(x), f(y), f(ww), f(hh)))
    w('};')
    w('')
    w('const zq_ui_layout layout_255 = { "2.55 (912x684, expanded)", 1, 912, 684, rects_255, (int)(sizeof(rects_255) / sizeof(rects_255[0])) };')
    w('')
    w("// 2.55's hotkeys where they differ from today's (the others keep today's binding)")
    w('const zq_ui_key keys_255[] =')
    w('{')
    for key, act, needs in KEYS:
        if act and not act.startswith('@') and act not in all_paths:
            print('warning: key %s -> %s is not a 2.53.1 menu path' % (key, act), file=sys.stderr)
        w('    { %s, %s, %s },' % (c_str(key), c_str(act), ('0x%X' % needs) if needs else '0'))
    w('};')
    w('')
    # dialogs per build (zquiinv's JSON), shared tables
    dtables = {}
    dnames = {}
    for b in BUILDS:
        d = dialogs.get(b)
        if not d:
            continue
        items = sorted(d.items())
        key = hashlib.sha1(json.dumps(items, sort_keys=True).encode()).hexdigest()
        if key not in dtables:
            dtables[key] = ('dialogs_' + b.replace('.', '_'), items)
        dnames[b] = dtables[key][0]
    for key, (name, items) in dtables.items():
        w('const zq_ui_dialog %s[] =' % name)
        w('{')
        for dn, js in items:
            s = js if isinstance(js, str) else json.dumps(js, separators=(',', ':'))
            # long string literals: split every 2000 characters (MSVC's limit is 16380 per piece)
            pieces = [s[i:i + 2000] for i in range(0, len(s), 2000)] or ['']
            w('    { %s,' % c_str(dn))
            for k, p in enumerate(pieces):
                w('      %s%s' % (c_str(p), ' },' if k == len(pieces) - 1 else ''))
        w('};')
        w('')
    for name, rows, bs in tables:
        for b in bs:
            dn = dnames.get(b)
            w('const zq_ui_profile p_%s = { "%s", 0x255, %s, (int)(sizeof(%s) / sizeof(%s[0])), &layout_255, &look_255, %s, %s, 0, keys_255, (int)(sizeof(keys_255) / sizeof(keys_255[0])) };'
              % (b.replace('.', '_'), b, name, name, name, dn or 'NULL',
                 '(int)(sizeof(%s) / sizeof(%s[0]))' % (dn, dn) if dn else '0'))
    w('')
    w('void add()')
    w('{')
    for name, rows, bs in tables:
        for b in bs:
            w('    zq_ui_add_profile(&p_%s);' % b.replace('.', '_'))
    w('    zq_ui_255::add_actions();')
    w('}')
    w('')
    w('zq_ui_registrar reg(add);')
    w('')
    w('} // namespace')
    open(a.out, 'w', encoding='utf-8', newline='\n').write('\n'.join(o) + '\n')

    print('%d builds, %d menu tables, %d dialog tables -> %s' % (len(BUILDS) - len(missing), len(tables), len(dtables), a.out))
    if missing:
        print('missing builds:', ', '.join(missing))
    for p, bs in sorted(g.unknown.items()):
        print('unmapped procedure %s (%s)' % (p, ', '.join(sorted(bs))))


if __name__ == '__main__':
    main()

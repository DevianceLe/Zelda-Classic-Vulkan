"""Writes src/port/zqeditor/zq_ui_profiles_300_data.h: the menus of every 3.0 build's ZQuest
(3.0.0-prerelease.1 to .239; our 3.0 Nightly shows the newest), mapped onto this ZQuest's editors.

    python tools/zq_gen_ui_profiles_300.py <zquiinv out folder>

Reads the inventory's menus_per_build.json (every build's menus as its source has them: texts,
procedures, submenus) and dialogs (dialog JSON, by build), and today's (2.53.1) menu paths from
src/zquest.cpp (the menu audit's names). Each upstream procedure is mapped onto one of today's
menu paths or onto an extra action of zq_ui_profiles_300.cpp ("@q30_..."); one with no editor here
is written with action "" (shown disabled, with the build's text). Builds whose menus are the same
share one table; a profile is registered where the menus change (zq_ui_profile.h: a profile covers
the builds after it up to the next one).
"""
import hashlib
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'src', 'port', 'zqeditor', 'zq_ui_profiles_300_data.h')

TUNES = ['Wind Fish', 'Overworld', 'Hyrule Castle', 'Lost Woods', 'Great Sea', 'East Hyrule',
         'Dancing Dragon', 'Stone Tower', 'Villages', 'Swamp + Desert', 'Outset Island',
         'Kakariko Village', 'Clock Town', 'Temple', 'Dark World', 'Dragon Roost', 'Horse Race',
         'Credits', "Zelda's Lullaby"]

# upstream procedure -> (action, needs). '' = no editor here (shown disabled).
MAP = {
    # File
    'do_NewQuest': 'File/New', 'onNew': 'File/New',
    'do_OpenQuest': 'File/Open', 'onOpen': 'File/Open', 'do_OpenQuestFile': 'File/Open',
    'onTileset': '',
    'onSave': 'File/Save', 'onSaveAs': 'File/Save as...', 'onRevert': 'File/Revert',
    'onQuestTemplates': 'File/Quest Templates...',
    'handle_quit': 'File/Exit', 'onExit': 'File/Exit',
    'onImport_Map': 'File/Import/Map', 'onImport_DMaps': 'File/Import/DMaps',
    'onImport_Tiles': 'File/Import/Tiles', 'onImport_Guys': 'File/Import/Enemies',
    'onImport_Subscreen': 'File/Import/Subscreen', 'onImport_Pals': 'File/Import/Palettes',
    'onImport_Msgs': 'File/Import/String Table', 'onImport_Combos': 'File/Import/Combo Table',
    'onImport_ComboAlias': 'File/Import/Combo Alias', 'onImport_ZGP': 'File/Import/Graphics Pack',
    'onImport_ZQT': 'File/Import/Quest Template', 'onImport_UnencodedQuest': 'File/Import/Unencoded Quest',
    'onExport_Map': 'File/Export/Map', 'onExport_DMaps': 'File/Export/DMaps',
    'onExport_Tiles': 'File/Export/Tiles', 'onExport_Guys': 'File/Export/Enemies',
    'onExport_Subscreen': 'File/Export/Subscreen', 'onExport_Pals': 'File/Export/Palettes',
    'onExport_Msgs': 'File/Export/String Table', 'onExport_MsgsText': 'File/Export/Text Dump',
    'onExport_Combos': 'File/Export/Combo Table', 'onExport_ComboAlias': 'File/Export/Combo Alias',
    'onExport_ZGP': 'File/Export/Graphics Pack', 'onExport_ZQT': 'File/Export/Quest Template',
    'onExport_UnencodedQuest': 'File/Export/Unencoded Quest',
    # Quest
    'onRulesDlg': '@q30_options', 'onRulesSearch': '@q30_options',
    'onHeader': 'Quest/Header', 'onMapCount': 'Quest/Map Count', 'onCheats': 'Quest/Cheats',
    'onCustomItems': 'Quest/Items', 'onCustomEnemies': 'Quest/Enemies',
    'onCustomHero': 'Quest/Graphics/Sprites/Link', 'onCustomLink': 'Quest/Graphics/Sprites/Link',
    'onStrings': '@q30_strings',
    'onDmaps': 'Quest/DMaps', 'onInit': 'Quest/Init Data',
    'onMaps': ('Edit/Maps/Scrolling Regions...', 0x300), 'onRegions': ('Edit/Maps/Scrolling Regions...', 0x300),
    'onZInfo': 'Quest/2.55/3.0 ZInfo...',
    'onTemplates': 'Quest/Template',
    'onEditSubscreens': 'Quest/Misc Data/Subscreens', 'onSubscreen': 'Quest/Misc Data/Master Subscreen Type',
    'onShopTypes': 'Quest/Misc Data/Shop Types', 'onInfoTypes': 'Quest/Misc Data/Info Types',
    'onBottleTypes': '@q30_bottle_types', 'onBottleShopTypes': '@q30_bottle_shops',
    'onWarpRings': 'Quest/Misc Data/Warp Rings', 'onTriPieces': 'Quest/Misc Data/Triforce Pieces',
    'onEndString': 'Quest/Misc Data/End String', 'onItemDropSets': 'Quest/Misc Data/Item Drop Sets',
    'onColors_Main': 'Quest/Graphics/Palettes/Main', 'onColors_Levels': 'Quest/Graphics/Palettes/Levels',
    'onColors_Sprites': 'Quest/Graphics/Palettes/Sprites',
    'onCustomWpns': 'Quest/Graphics/Sprites/Weapons/Misc', 'onMiscSprites': '@q30_misc_sprites',
    'onCombos': 'Quest/Graphics/Combos', 'onTiles': 'Quest/Graphics/Tiles',
    'onIcons': 'Quest/Graphics/Game icons', 'onMiscColors': 'Quest/Graphics/Misc colors',
    'onMapStyles': 'Quest/Graphics/Map styles', 'onDoorCombos': 'Quest/Graphics/Door Combo Sets',
    'onEditComboAlias': 'Quest/Graphics/Combo Aliases', 'lambda:call_alias_pages': 'Quest/Graphics/Combo Aliases',
    'onEditComboPool': 'Quest/Graphics/Combo Pools...', 'lambda:call_cpool_pages': 'Quest/Graphics/Combo Pools...',
    'onEditAutoCombo': 'Quest/Graphics/Autocombos...', 'lambda:call_autoc_pages': 'Quest/Graphics/Autocombos...',
    'onSelectSFX': 'Quest/Audio/SFX Data', 'onMidis': 'Quest/Audio/MIDIs',
    'onMusic': 'Quest/Audio/3.0 Music...', 'onMiscSFX': '@q30_misc_sfx',
    'onDefault_Pals': 'Quest/Defaults/Palettes', 'onDefault_Tiles': 'Quest/Defaults/Tiles',
    'onDefault_Combos': 'Quest/Defaults/Combos', 'onDefault_Items': 'Quest/Defaults/Items',
    'onDefault_Guys': 'Quest/Defaults/Enemies', 'onDefault_Weapons': 'Quest/Defaults/Weapon Sprites',
    'onDefault_MapStyles': 'Quest/Defaults/Map Styles', 'onDefault_SFX': 'Quest/Defaults/SFX Data',
    'onImportFFScript': 'Quest/Scripts/Import ASM FFC Script',
    'onImportItemScript': 'Quest/Scripts/Import ASM Item Script',
    'onImportGScript': 'Quest/Scripts/Import ASM Global Script',
    'onCompileScript': 'Quest/Scripts/Compile ZScript...',
    'onZScriptSettings': '@q30_script_rules', 'onZScriptCompilerSettings': '@q30_compiler_settings',
    # Edit
    'onUndo': 'Edit/Undo', 'onCopy': 'Edit/Copy', 'onPaste': 'Edit/Paste', 'onPasteAll': 'Edit/Paste All',
    'onPasteToAll': 'Edit/Adv. Paste/Paste To All', 'onPasteAllToAll': 'Edit/Adv. Paste/Paste All To All',
    'onPasteUnderCombo': 'Edit/Paste Spec./Undercombo', 'onPasteSecretCombos': 'Edit/Paste Spec./Secret Combos',
    'onPasteFFCombos': 'Edit/Paste Spec./Freeform Combos', 'onPasteScreenData': 'Edit/Paste Spec./Screen Data',
    'onPasteWarps': 'Edit/Paste Spec./Warps', 'onPasteWarpLocations': 'Edit/Paste Spec./Warp Return',
    'onPasteEnemies': 'Edit/Paste Spec./Enemies', 'onPasteRoom': 'Edit/Paste Spec./Room Type Data',
    'onPasteGuy': 'Edit/Paste Spec./Guy/String', 'onPasteDoors': 'Edit/Paste Spec./Doors',
    'onPasteLayers': 'Edit/Paste Spec./Layers', 'onPastePalette': 'Edit/Paste Spec./Palette',
    'onDelete': 'Edit/Delete',
    'onGotoMap': 'Edit/Maps/Goto Map...', 'onIncMap': 'Edit/Maps/Next Map', 'onDecMap': 'Edit/Maps/Previous Map',
    'onDeleteMap': 'Edit/Maps/Delete Map',
    # View
    'onViewMap': 'View/View Map...', 'onShowPal': 'View/View Palette',
    'onShowWalkability': 'View/Show Walkability', 'onShowFlags': 'View/Show Flags',
    'onShowCSet': 'View/Show CSets', 'onShowCType': 'View/Show Types',
    'onToggleShowInfo': 'View/Show Screen Info', 'onToggleShowSquares': 'View/Show Squares',
    'onToggleShowScripts': 'View/Show Script Names',
    'onToggleGrid': 'View/Show Grid', 'onGridToggle': 'View/Show Grid',
    'onShowDarkness': '@q30_darkness',
    # Tools
    'onFlags': 'Tools/Combo Flags', 'onCSetFix': 'Tools/Color Set Fix',
    'onTemplate': 'Tools/NES Dungeon Template', 'onReTemplate': 'Tools/Apply Template to All',
    'onPreviewMode': 'Tools/Preview Mode',
    'onDrawingModeNormal': 'Tools/Drawing Mode/Normal', 'onDrawingModeRelational': 'Tools/Drawing Mode/Relational',
    'onDrawingModeDungeon': 'Tools/Drawing Mode/Dungeon Carving', 'onDrawingModeAlias': 'Tools/Drawing Mode/Combo Alias',
    'onDrawingModePool': ('@pool', 0x255), 'onDrawingModeAuto': ('@auto', 0x255),
    'onUsedCombos': 'Tools/List Combos Used',
    'onComboLocationReport': 'Tools/Quest Reports/Combo Locations',
    'onComboTypeLocationReport': 'Tools/Quest Reports/Combo Type Locations',
    'onEnemyLocationReport': 'Tools/Quest Reports/Enemy Locations',
    'onItemLocationReport': 'Tools/Quest Reports/Item Locations',
    'onScriptLocationReport': 'Tools/Quest Reports/Script Locations',
    'onWhatWarpsReport': 'Tools/Quest Reports/What Links Here',
    'onIntegrityCheckAll': 'Tools/Quest Reports/Integrity Check/All',
    'onIntegrityCheckWarps': 'Tools/Quest Reports/Integrity Check/Warps',
    'onIntegrityCheckRooms': 'Tools/Quest Reports/Integrity Check/Screens',
    # Screen
    'onScrData': '@q30_screen_data', 'onSelectFFCombo': 'Screen/Freeform Combos', 'onLayers': 'Screen/Layers',
    'onTileWarp': 'Screen/Tile Warp', 'onSideWarp': 'Screen/Side Warp', 'onSecretCombo': 'Screen/Secret Combos',
    'onUnderCombo': 'Screen/Under Combo', 'onDoors': 'Screen/Doors', 'onPath': 'Screen/Maze Path',
    'onItem': 'Screen/Item', 'onEnemies': 'Screen/Enemies', 'onScreenPalette': 'Screen/Palette',
    'onRoom': '@q30_room', 'onGuy': 'Screen/Guy', 'onString': 'Screen/Message String',
    'onRType': 'Screen/Room Type', 'onCatchall': 'Screen/Catch All',
    'onScreenNotes': 'Screen/2.55/3.0 Screen Data...', 'lambda:edit_screen_notes': 'Screen/2.55/3.0 Screen Data...',
    # Etc
    'onHelp': 'Etc/Help', 'onOldHelp': 'Etc/Help', 'onAbout': 'Etc/About',
    'onZQVidMode': 'Etc/Video Settings...', 'onOptions': 'Etc/Options...', 'onFullScreen': 'Etc/Fullscreen',
    'onViewPic': 'Etc/View Pic...', 'playMusic': 'Etc/Play music', 'changeTrack': 'Etc/Change track',
    'stopMusic': 'Etc/Stop tunes', 'onClearQuestFilepath': 'Etc/Clear Quest Filepath',
    'onSnapshot': 'Etc/Take Snapshot', 'onMenuSnapshot': 'Etc/Take Snapshot',
    'onSaveZQuestSettings': 'Etc/Save ZQuest Configuraton',
}
for i, t in enumerate(TUNES):
    MAP['playTune%d' % (i + 1)] = 'Etc/The Travels of Link/' + t

# what the extra actions need (zq_ui_profiles_300.cpp registers them with the same needs), and today's
# entries for what only newer quests hold
ACTION_NEEDS = {
    'Quest/Graphics/Combo Pools...': 0x255, 'Quest/Graphics/Autocombos...': 0x255,
    'Quest/2.55/3.0 ZInfo...': 0x255, 'Quest/Audio/3.0 Music...': 0x300,
    'Screen/2.55/3.0 Screen Data...': 0x255, 'Edit/Maps/Scrolling Regions...': 0x300,
    '@q30_bottle_types': 0x255, '@q30_bottle_shops': 0x255, '@q30_misc_sprites': 0x255,
    '@q30_misc_sfx': 0x255, '@q30_script_rules': 0x255, '@q30_compiler_settings': 0x255,
}
# known procedures with no editor here (so they aren't reported as unmapped)
NONE = set('''onTestQst onRedo onSaveMenus onQMiscValues onSlotAssign onImportZASM onExportZASM
onImport_Tilepack onImport_Tilepack_To onImport_Combopack onImport_Combopack_To onImport_Comboaliaspack
onImport_Comboaliaspack_To onImport_Doorset onImport_StringsTSV onExport_Tilepack onExport_Combopack
onExport_Comboaliaspack onExport_Doorset onExport_StringsTSV onExport_Package onZstringshelp
do_zq_hotkey_dialog do_zq_list_hotkeys_dialog toggleConsole onMapscrSnapshot playZCForever onGlobalInitD
onBuggedNextComboLocationReport onDMapLevelsReport onWaterSolidity onEffectFix onPalFix onPitFix onStrFix
onRemoveOldArrivalSquare std std::bind call_screen_triggers_dialog browse_screen_notes onToggleShowFFCs
onToggleScreenGrid onToggleRegionGrid onToggleCurrentScreenOutline onLayer2BG onLayer3BG
onToggleHighlightLayer onToggleHighQualityScreenRendering'''.split())


def paths_253():
    """Today's menu paths, as the menu audit writes them (src/zquest.cpp's MENU tables)."""
    src = open(os.path.join(ROOT, 'src', 'zquest.cpp'), encoding='latin-1').read()
    lines, skip = [], []
    for line in src.split('\n'):
        s = line.strip()
        if s.startswith('#ifdef ZC_PORT') or s.startswith('#ifdef ZC_ZQEDITOR'):
            skip.append(False); continue
        if s.startswith('#ifndef ZC_PORT'):
            skip.append(True); continue
        if skip and s.startswith('#else'):
            skip[-1] = not skip[-1]; continue
        if skip and s.startswith('#endif'):
            skip.pop(); continue
        if not any(skip):
            lines.append(line)
    src = '\n'.join(lines)
    row = re.compile(r'\{\s*(?:\(char\s*\*\)\s*"((?:[^"\\]|\\.)*)"|NULL)\s*,\s*(\w+)\s*,\s*(?:\(MENU\s*\*\)\s*)?(\w+)\s*,')
    menus = {}
    for m in re.finditer(r'MENU\s+(\w+)\[\]\s*=\s*\{(.*?)\n\};', src, re.S):
        rows = []
        for line in m.group(2).split('\n'):
            if line.strip().startswith('//'):
                continue
            e = row.search(line)
            if e:
                rows.append(e.groups())
        menus[m.group(1)] = rows
    out = set()

    def walk(name, prefix):
        for t, proc, child in menus.get(name, []):
            if t is None:
                continue
            t = t.replace('\\t', '\t').split('\t')[0].replace('&', '').rstrip(' ')
            here = prefix + ('Catch All' if proc == 'onCatchall' else t)
            if child != 'NULL':
                walk(child, here + '/')
            elif proc != 'NULL':
                out.add(here)
    walk('the_menu', '')
    return out


def prerelease(name):
    m = re.match(r'^(?:3\.0\.0-prerelease\.|3\.0\.p)(\d+)', name)
    return int(m.group(1)) if m else None


def upname(b):
    return '3.0.0-prerelease.%d' % prerelease(b)


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\t', '\\t') + '"'


def main():
    inv = sys.argv[1] if len(sys.argv) > 1 else r'H:\mhfix\zquiinv\out'
    with open(os.path.join(inv, 'menus_per_build.json'), encoding='utf-8') as f:
        data = json.load(f)
    valid = paths_253()
    builds = sorted([b for b in data if prerelease(b) is not None], key=prerelease)
    unmapped = {}
    tables = {}         # fingerprint -> (name, rows)
    runs = []           # (first build, last build, fingerprint)
    for b in builds:
        d = data[b]
        rows = []
        for top in d['root']:
            rows.append((0, top.get('text', ''), None, 0))
            rows.extend(rows_of_items(b, top.get('items', []), 1, valid, unmapped))
        fp = hashlib.sha1(json.dumps(rows).encode()).hexdigest()[:12]
        if fp not in tables:
            tables[fp] = ('menu_%03d' % prerelease(b), rows)
        if runs and runs[-1][2] == fp:
            runs[-1][1] = b
        else:
            runs.append([b, b, fp])
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('// zq_ui_profiles_300_data.h: the menus of 3.0\'s ZQuest builds (3.0.0-prerelease.1 to .%d),\n'
                % prerelease(builds[-1]))
        f.write('// mapped onto this ZQuest\'s editors. Generated by tools/zq_gen_ui_profiles_300.py from the\n')
        f.write('// build inventory\'s menus (each build\'s own source): do not edit by hand.\n')
        f.write('// %d builds, %d different menu sets.\n\n' % (len(builds), len(tables)))
        f.write('#pragma once\n\nnamespace zq30ui\n{\n\n')
        for fp, (name, rows) in tables.items():
            first = [r for r in runs if r[2] == fp]
            f.write('// %s\n' % ', '.join(upname(r[0]) if r[0] == r[1] else upname(r[0]) + ' to .%d' % prerelease(r[1]) for r in first))
            f.write('const zq_ui_menu_row %s[] =\n{\n' % name)
            for depth, text, action, needs in rows:
                a = 'NULL' if action is None else cstr(action)
                n = ', 0x%x' % needs if needs else ''
                f.write('    { %d, %s, %s%s },\n' % (depth, cstr(text), a, n))
            f.write('};\n\n')
        f.write('struct build_menus\n{\n    const char *build;      // the first build that has these menus\n'
                '    const char *last;       // the last one before the menus change\n'
                '    int first_n, last_n;    // their prerelease numbers\n    const zq_ui_menu_row *menu;\n    int rows;\n};\n\n')
        f.write('const build_menus g_builds[] =\n{\n')
        for first, last, fp in runs:
            name = tables[fp][0]
            f.write('    { %s, %s, %d, %d, %s, sizeof(%s) / sizeof(%s[0]) },\n' % (cstr(upname(first)), cstr(upname(last)), prerelease(first), prerelease(last), name, name, name))
        f.write('};\n\n} // namespace zq30ui\n')
    print('%d builds, %d menu sets, %d profiles -> %s' % (len(builds), len(tables), len(runs), OUT))
    for k, v in sorted(unmapped.items()):
        vs = sorted(v, key=prerelease)
        print('unmapped %-40s %d builds (%s .. %s)' % (k, len(vs), vs[0], vs[-1]))


def rows_of_items(build, items, depth, valid, unmapped):
    rows = []
    for it in items:
        if it.get('sep'):
            rows.append((depth, '', None, 0))
            continue
        if it.get('recent_slot'):
            rows.append((depth, '---', '', 0))
            continue
        text = it.get('text', '')
        if it.get('hotkey'):
            text = text + '\t' + it['hotkey']
        if 'items' in it:
            rows.append((depth, text, None, 0))
            rows.extend(rows_of_items(build, it['items'], depth + 1, valid, unmapped))
            continue
        proc = it.get('proc') or ''
        m = MAP.get(proc)
        if m is None:
            if proc and proc not in NONE:
                unmapped.setdefault(proc, set()).add(build)
            m = ''
        action, needs = (m if isinstance(m, tuple) else (m, ACTION_NEEDS.get(m, 0)))
        if action and not action.startswith('@') and action not in valid:
            unmapped.setdefault('path:' + action, set()).add(build)
            action = ''
        rows.append((depth, text, action, needs))
        if proc == 'onOptions':
            rows.append((depth, 'Interface...', '@interface', 0))
    return rows


if __name__ == '__main__':
    main()

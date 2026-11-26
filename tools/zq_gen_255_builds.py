# Writes src/port/zqeditor/zq_builds_255_data.h: every 2.55 release ZQuest can start a new quest in or
# save a quest as (2.55.0 to 2.55.19), from upstream's own source at each release's commit:
#   - its date (the release commit's), for zq_build::order;
#   - the three section versions that changed during 2.55 (src/base/zdefs.h: V_MAPS, V_GUYS,
#     V_SUBSCREEN; every other V_ and CV_ is the same in all of them, checked here);
#   - its header's version fields (src/metadata/versionsig.h V_ZC_*; VERSION_BUILD 61 in all);
#   - its File > New: the Classic tileset it ships (resources/modules/classic/default.qst, by blob),
#     the Cambria one (resources/tilesets/cambria.qst, a Git LFS file), and how onNew starts a quest
#     (2.55.0-2.55.7 the Classic quest; 2.55.8-2.55.10 a choice of tileset; 2.55.11 on the wizard);
#   - its rulesets and rule templates (tools/zq_gen_new_rulesets.py's lists, per release).
#   python -I tools/zq_gen_255_builds.py <upstream repo> <out .h> [<templates dir to extract Classic files into>]
import os, re, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zq_gen_new_rulesets as rs_gen   # noqa: E402

REPO = sys.argv[1]
OUT = sys.argv[2]
TPL = sys.argv[3] if len(sys.argv) > 3 else None
# upstream's tags 2.55.0 .. 2.55.18; 2.55.19 is the bump commit on origin/releases/2.55
RELEASES = [('2.55.%d' % i, '2.55.%d' % i) for i in range(19)] + [('2.55.19', '9ecadf4c2')]


def git(*a):
    return subprocess.run(['git', '-C', REPO] + list(a), check=True, capture_output=True).stdout


def show(rev, path):
    return git('show', '%s:%s' % (rev, path)).decode('utf-8', 'replace')


def defines(text, prefix):
    return dict(re.findall(r'^#define\s+(%s\w*)\s+(\d+)' % prefix, text, re.M))


def blob(rev, path):
    out = git('ls-tree', rev, path).decode().split()
    return out[2] if len(out) >= 3 else None


rows, sets, set_of = [], [], {}
base_v = base_cv = None
classic_files = {}          # blob -> template file name
for name, rev in RELEASES:
    patch = int(name.split('.')[2])
    date = git('log', '-1', '--format=%ad', '--date=format:%Y%m%d', rev).decode().strip()
    zdefs = show(rev, 'src/base/zdefs.h')
    v, cv = defines(zdefs, 'V_'), defines(zdefs, 'CV_')
    sig = defines(show(rev, 'src/metadata/versionsig.h'), 'V_ZC_')
    assert sig['V_ZC_FIRST'] == '2' and sig['V_ZC_SECOND'] == '55' and int(sig['V_ZC_THIRD']) == patch, name
    assert defines(zdefs, 'VERSION_BUILD')['VERSION_BUILD'] == '61', name
    # every section version but these three is the same in every release
    same_v = {k: x for k, x in v.items() if k not in ('V_MAPS', 'V_GUYS', 'V_SUBSCREEN')}
    if base_v is None:
        base_v, base_cv = same_v, cv
    assert same_v == base_v and cv == base_cv, name
    classic = blob(rev, 'resources/modules/classic/default.qst')
    cambria = blob(rev, 'resources/tilesets/cambria.qst')
    if classic not in classic_files:
        classic_files[classic] = 'templates/classic_%s.qst' % name
    # the newest Classic is the one 2.55.19's New already uses
    zf = show(rev, 'src/zq/zq_files.cpp')
    onnew = zf[zf.index('int32_t onNew()'):]
    onnew = onnew[:onnew.index('\n}\n')]
    flow = 2 if 'call_tileset_wizard' in onnew else 1 if 'cambria' in onnew else 0
    # the rule lists
    tree = os.path.join(os.environ.get('TEMP', '.'), 'zq_gen_255_tree')
    for sub in ('src/base', 'src/zq', 'src/dialog'):
        os.makedirs(os.path.join(tree, sub), exist_ok=True)
    for p in ('src/base/qrs.h', 'src/zq/zq_files.cpp', 'src/dialog/quest_rules.cpp'):
        open(os.path.join(tree, p), 'w', encoding='utf-8').write(show(rev, p))
    nums, L, rs = rs_gen.gen(tree, '/src/base/qrs.h')
    lists = {}
    for n, names in L.items():
        if names:
            lists[n.strip('_').upper()] = [nums[x] for x in names]
    for k in ('NES', 'FIXEDNES', 'BSZ', 'Z3', 'MODERN'):
        pairs = []
        for lst, val in rs[k]:
            pairs += [(nums[x], int(val)) for x in L[lst]]
        lists['RS_' + k] = pairs
        lists['RS_' + k + '_TEMPLATES'] = rs[k + '_TMPL']
    for single in ('qr_OLD_STRING_EDITOR_MARGINS', 'qr_STRING_FRAME_OLD_WIDTH_HEIGHT', 'qr_CORRECTED_EW_BRANG_ANIM',
                   'qr_PARSER_SHORT_CIRCUIT', 'qr_PARSER_TRUE_INT_SIZE', 'qr_ANIMATECUSTOMWEAPONS'):
        lists[single[3:]] = nums[single]
    lists['MAX'] = nums['qr_MAX']
    key = repr(sorted(lists.items()))
    if key not in set_of:
        set_of[key] = len(sets)
        sets.append((name, lists))
    rows.append(dict(name=name, patch=patch, date=date, rev=rev, maps=int(v['V_MAPS']), guys=int(v['V_GUYS']),
                     subs=int(v['V_SUBSCREEN']), classic=classic, cambria=cambria, flow=flow, set=set_of[key]))

# the Classic files: the newest is templates/classic_2.55.qst (shipped already); the others by release
newest = rows[-1]['classic']
classic_files[newest] = 'templates/classic_2.55.qst'
if TPL:
    os.makedirs(TPL, exist_ok=True)
    for b, f in classic_files.items():
        if b != newest:
            open(os.path.join(TPL, os.path.basename(f)), 'wb').write(git('cat-file', 'blob', b))
# Cambria is a Git LFS file: only the newest one (2.55.13 on, templates/cambria.qst) is on hand; the
# releases with an older Cambria (2.55.8 to 2.55.12) start from that one
cambria_newest = rows[-1]['cambria']

o = ['// Generated by tools/zq_gen_255_builds.py from upstream\'s source at each 2.55 release (its tags 2.55.0 to',
     '// 2.55.18, and 2.55.19\'s bump commit 9ecadf4c2 on releases/2.55). Do not edit.', '',
     '#ifndef ZQ_BUILDS_255_DATA_H', '#define ZQ_BUILDS_255_DATA_H', '']
for i, (first, lists) in enumerate(sets):
    p = 'zqr255r%d_' % i
    o.append('// rule lists of %s and later releases with the same lists' % first)
    for n, x in lists.items():
        if isinstance(x, bool):
            o.append('static const bool %s%s = %s;' % (p, n, 'true' if x else 'false'))
        elif isinstance(x, int):
            o.append('static const short %s%s = %d;' % (p, n, x))
        elif x and isinstance(x[0], tuple):
            o.append('static const short %s%s[] = { %s, -1 };   // rule, value' % (p, n, ', '.join('%d,%d' % t for t in x)))
        else:
            o.append('static const short %s%s[] = { %s, -1 };' % (p, n, ', '.join(str(y) for y in x)))
    o.append('')
o.append('struct Zq255RuleLists')
o.append('{')
o.append('    const short *compat, *zs_on, *zs_off, *keep, *always, *sub_off, *sub_on;')
o.append('    const short *rs[6];')
o.append('    bool rs_templates[6];')
o.append('    short margins, frame, brang, short_circuit, true_int, animate_custom, max;')
o.append('};')
o.append('')
o.append('static const Zq255RuleLists zq255_rule_lists[] =')
o.append('{')
for i, (first, lists) in enumerate(sets):
    p = 'zqr255r%d_' % i
    o.append('    { %sCOMPAT, %sZSONRULES, %sZSOFFRULES, %sKEEP, %sRALWAYS_ON, %sSUBSCR_RULES_OFF, %sSUBSCR_RULES_ON,' % ((p,) * 7))
    o.append('      { NULL, %sRS_NES, %sRS_FIXEDNES, %sRS_BSZ, %sRS_Z3, %sRS_MODERN },' % ((p,) * 5))
    o.append('      { false, %sRS_NES_TEMPLATES, %sRS_FIXEDNES_TEMPLATES, %sRS_BSZ_TEMPLATES, %sRS_Z3_TEMPLATES, %sRS_MODERN_TEMPLATES },' % ((p,) * 5))
    o.append('      %sOLD_STRING_EDITOR_MARGINS, %sSTRING_FRAME_OLD_WIDTH_HEIGHT, %sCORRECTED_EW_BRANG_ANIM, %sPARSER_SHORT_CIRCUIT,' % ((p,) * 4))
    o.append('      %sPARSER_TRUE_INT_SIZE, %sANIMATECUSTOMWEAPONS, %sMAX },   // %s' % (p, p, p, first))
o.append('};')
o.append('')
o.append('// New: 0 the Classic quest (onNew: NewQuestFile, the ruleset, the bug fix template); 1 the same from a')
o.append('// tileset the user picks (Cambria or Classic); 2 the tileset wizard.')
o.append('struct Zq255Release')
o.append('{')
o.append('    const char *name;')
o.append('    int patch;')
o.append('    long date;              // the release commit\'s, YYYYMMDD')
o.append('    int v_maps, v_guys, v_subscreen;')
o.append('    const char *classic;    // its Classic tileset, as shipped in templates')
o.append('    const char *cambria;    // its Cambria tileset (NULL before 2.55.8)')
o.append('    bool own_cambria;       // false: an older Cambria, not on hand; the newest stands in')
o.append('    int new_flow;')
o.append('    int rules;              // its zq255_rule_lists entry')
o.append('};')
o.append('')
o.append('static const Zq255Release zq255_releases[] =')
o.append('{')
for r in rows:
    cam = '"templates/cambria.qst"' if r['cambria'] else 'NULL'
    own = 'true' if (not r['cambria'] or r['cambria'] == cambria_newest) else 'false'
    o.append('    { "%s", %d, %sL, %d, %d, %d, "%s", %s, %s, %d, %d },' % (
        r['name'], r['patch'], r['date'], r['maps'], r['guys'], r['subs'], classic_files[r['classic']], cam, own, r['flow'], r['set']))
o.append('};')
o.append('static const int zq255_release_count = (int)(sizeof(zq255_releases) / sizeof(zq255_releases[0]));')
o.append('')
o.append('#endif')
open(OUT, 'w', newline='\n').write('\n'.join(o) + '\n')
print('wrote', OUT, len(rows), 'releases,', len(sets), 'rule list sets,', len(classic_files), 'Classic files')

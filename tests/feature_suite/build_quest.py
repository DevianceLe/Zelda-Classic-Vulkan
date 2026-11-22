# Builds quests\suite30.qst from features.txt: upstream's z3 test quest (saved as the newest 3.0 format)
# + one map copy per scene with its edits + one DMap per feature + the scripts, compiled with our ZQuest.
#   python -I build_quest.py <z3new.qst> <our build folder (zqeditor.exe with the -qst-scene-edit tool)> [work dir]
# The -qst-scene-edit switch is a test-only ZQuest patch (not part of the program; README.txt says where it
# is). The quest is saved in its own version (3.0) by our ZQuest at each step.
import os, re, sys, subprocess, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
base, rel = sys.argv[1], sys.argv[2]
work = sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, '_build')
os.makedirs(work, exist_ok=True)

SCRIPT_MAP, FIRST_MAP, FIRST_ENEMY = 20, 21, 230
TE_TESTS = {'sideswim', 'dive', 'dive2', 'dive4', 'swim2', 'pitkb', 'pitdrop', 'wpn', 'regs', 'trig', 'plain',
            'bitmaps', 'files', 'heights', 'stack'}


def features():
    out = []
    for l in open(os.path.join(HERE, 'features.txt'), encoding='utf-8'):
        if not l.strip() or l.startswith('#'):
            continue
        p = [x.strip() for x in l.split('|')]
        out.append(dict(name=p[0], dmap=int(p[1]), scr=int(p[2]), scene=p[3], script=p[4]))
    return out


F = features()
ed = ['# suite edits (build_quest.py)']
# DMaps first: each a copy of DMap 0
for f in F:
    if f['dmap'] != 0:
        ed.append('dmap %d copy 0' % f['dmap'])
# the script room map: map 1's screen 0x14 without enemies
ed.append('map %d copy 1' % SCRIPT_MAP)
s14 = SCRIPT_MAP * 136 + 0x14
ed += ['screen %d enemy%d 0' % (s14, k) for k in range(10)] + ['screen %d pattern 0' % s14, 'screen %d enemyflags 0' % s14]
scenes = {}
next_map, next_enemy = FIRST_MAP, FIRST_ENEMY
for f in F:
    sc = f['scene']
    if sc == '@script':
        ed.append('dmap %d map %d' % (f['dmap'], SCRIPT_MAP))
        continue
    if sc == '-':
        continue
    if sc not in scenes:
        m = next_map; next_map += 1
        scenes[sc] = m
        ed.append('# scene %s: map %d' % (sc, m))
        ed.append('map %d copy 1' % m)
        swaps = {}; later = []
        for part in sc.split('+'):
            for l in open(os.path.join(HERE, 'scenes', part), encoding='utf-8'):
                t = l.split()
                if len(t) != 4 or t[0].startswith('#'):
                    continue
                kind, i, key, val = t[0], int(t[1]), t[2], t[3]
                if kind == 'screen' and 136 <= i < 272:
                    ed.append('screen %d %s %s' % (i - 136 + m * 136, key, val))
                elif kind == 'enemy':
                    if i not in swaps:
                        swaps[i] = next_enemy; next_enemy += 1
                        later.append('enemy %d copy %d' % (swaps[i], i))
                    later.append('enemy %d %s %s' % (swaps[i], key, val))
                elif kind == 'dmap':
                    later.append('@dmap %s %s' % (key, val))
                else:
                    later.append(' '.join(t))
        for l in later:
            if not l.startswith('@'):
                ed.append(l)
        for e, ne in swaps.items():
            ed.append('map %d enemyswap.%d %d' % (m, e, ne))
        scenes[sc] = (m, [l[1:] for l in later if l.startswith('@')])
    m, dm_edits = scenes[sc]
    ed.append('dmap %d map %d' % (f['dmap'], m))
    for l in dm_edits:
        ed.append('dmap %d %s' % (f['dmap'], l.split(' ', 1)[1]))
edits = os.path.join(work, 'suite_edits.txt')
open(edits, 'w', newline='\n').write('\n'.join(ed) + '\n')

env = dict(os.environ, SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0', SDL_JOYSTICK_HIDAPI='0')
empty = os.path.join(work, 'empty.txt'); open(empty, 'w').close()
env['ZQ_UI_SCRIPT'] = empty
zq = os.path.join(rel, 'zqeditor.exe')
mid = os.path.join(work, 'suite_edit.qst')
r = subprocess.run([zq, '-qst-scene-edit', base, mid, edits, os.path.join(work, 'edit_report.txt')], cwd=rel, env=env,
                   capture_output=True, timeout=900)
print('scene edits:', open(os.path.join(work, 'edit_report.txt')).read().strip()[:300])
if r.returncode:
    sys.exit(1)

# --- the script buffer: z3's own scripts, each feature script in its own namespace, the dispatcher ---
def body(path):
    t = open(path, encoding='utf-8').read()
    t = re.sub(r'^\s*(#include|import)\s+"std\.zh"\s*$', '', t, flags=re.M)
    return re.sub(r'\bglobal\s+script\s+\w+', 'namespace G', t)

buf = ['#include "std.zh"', '']
for z in ('scrolling_debug.zs', 'ffc_hello_world.zs', 'trigger_on_cursor.zs'):
    buf.append(body(os.path.join(HERE, 'scripts', 'z3', z)))
rules = [l.split()[:2] for l in open(os.path.join(HERE, 'scripts', 'te_rules.txt')) if l[0].isdigit()]
buf.append('// Tranquil Expression\'s rules for the tests written in it (scripts\\te_rules.txt)\nvoid suite_te_rules()\n{')
buf += ['\tGame->FFRules[%s] = %s;' % (n, 'true' if v == '1' else 'false') for n, v in rules]
# (the z3 quest's 3.0 HP per heart gives other life numbers in our player: start_state shows that once)
buf.append('\tHero->MaxHP = 48;\n\tHero->HP = 48;\n}\n')
calls = []
for f in F:
    if f['script'] == '-':
        continue
    src = os.path.join(HERE, 'coverage' if f['script'].startswith('cov_') else 'scripts', f['script'])
    ns = 'S_' + re.sub(r'\W', '_', f['name'])
    buf.append('namespace %s\n{\n%s\n}\n' % (ns, body(src)))
    pre = 'suite_te_rules(); ' if os.path.splitext(f['script'])[0] in TE_TESTS else ''
    calls.append('\t\t%sif(d == %d) { %s%s::G::run(); }' % ('else ' if calls else '', f['dmap'], pre, ns))
buf.append('global script Active\n{\n\tvoid run()\n\t{\n\t\tint d = Game->GetCurDMap();\n%s\n\t\twhile(true) Waitframe();\n\t}\n}\n' % '\n'.join(calls))
zs = os.path.join(work, 'suite.zs')
open(zs, 'w', newline='\n').write('\n'.join(buf))
out = os.path.join(HERE, 'quests', 'suite30.qst')
os.makedirs(os.path.dirname(out), exist_ok=True)
env['ZQ_COMPILE_BUFFER'] = zs
rep = os.path.join(work, 'compile_report.txt')
r = subprocess.run([zq, '-qst-compile', mid, out, 'smart', rep], cwd=rel, env=env, capture_output=True, timeout=900)
txt = open(rep, encoding='utf-8', errors='replace').read()
errs = [l for l in txt.split('\n') if '[-9997]' in l][:12]
print('compile: exit %d; %s' % (r.returncode, ' | '.join(l.strip() for l in txt.split('\n') if l.startswith('result') or l.startswith('save'))))
for l in errs:
    print('  ' + l.strip()[:200])
if r.returncode == 0:
    print('quest: %s (%d bytes)' % (out, os.path.getsize(out)))

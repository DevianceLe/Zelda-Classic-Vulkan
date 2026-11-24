#!/usr/bin/env python3
# Feature test suite: plays every feature of tests/feature_suite/features.txt in our player (several at a
# time, windows shown but never focused) and compares it with what upstream 3.0 did on the same input,
# recorded once in tests/feature_suite/expected (picture hashes, Link's positions, script lines).
#
#   python tools/feature_suite.py [names or groups...] [--jobs 6] [--build build/Release] [--work DIR] [--keep]
#   python tools/feature_suite.py list
#   python tools/feature_suite.py record [names...] --upstream <upstream 3.0 build folder>   (once; upstream only)
#
# One line per feature: SAME, or DIFF and where (hero frames, script lines, picture rows/columns), MISSING
# (no expected data), FAILED (our player did not run); then the total and, for the coverage groups, how many
# of the script commands and registers behave as upstream's.
import os, re, sys, json, time, glob, shutil, zlib, hashlib, subprocess, threading, argparse
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUITE = os.path.join(ROOT, 'tests', 'feature_suite')
QUEST = os.path.join(SUITE, 'quests', 'suite30.qst')
EXP = os.path.join(SUITE, 'expected')
KEYS_ZCR = {'UP': 'UP', 'DOWN': 'DOWN', 'LEFT': 'LEFT', 'RIGHT': 'RIGHT', 'A': 'Z', 'B': 'X', 'START': 'ENTER',
            'L': 'A', 'R': 'S', 'EX1': 'Q', 'EX2': 'W'}
KEYS_UP = {'UP': 'Up', 'DOWN': 'Down', 'LEFT': 'Left', 'RIGHT': 'Right', 'A': 'A', 'B': 'B', 'START': 'Start',
           'L': 'L', 'R': 'R', 'EX1': 'Ex1', 'EX2': 'Ex2'}
LINE_RE = re.compile(r'^(T[A-Z0-9]*|C): ')
NOWIN = 0x08000000 if os.name == 'nt' else 0      # CREATE_NO_WINDOW (upstream is headless anyway)


def features():
    out = []
    for l in open(os.path.join(SUITE, 'features.txt'), encoding='utf-8'):
        if not l.strip() or l.startswith('#'):
            continue
        p = [x.strip() for x in l.split('|')]
        if p[6] == 'auto':      # the script's own "// frames: N" line
            src = os.path.join(SUITE, 'coverage' if p[4].startswith('cov_') else 'scripts', p[4])
            p[6] = re.search(r'^// frames: *(\d+)', open(src, encoding='utf-8').read(), re.M).group(1)
        f = dict(name=p[0], dmap=int(p[1]), scr=int(p[2]), scene=p[3], script=p[4], frames=int(p[6]), what=p[9])
        f['keys'] = [] if p[5] == '-' else [(int(k.split(':')[0]), k.split(':')[1][0] == '+', k.split(':')[1][1:]) for k in p[5].split()]
        f['snaps'] = [] if p[7] == '-' else [int(x) for x in p[7].split()]
        chk = p[8].split()
        f['hero'] = 'hero' in chk or any(c.startswith('hero=') for c in chk)
        hr = next((c[5:] for c in chk if c.startswith('hero=')), None)
        f['hero_range'] = [int(v) for v in hr.split('-')] if hr else None
        f['random'] = 'random' in chk
        f['lines'] = 'lines' in chk
        f['rows'] = next((int(c[5:]) for c in chk if c.startswith('rows=')), 0)
        crop = next((c[5:] for c in chk if c.startswith('crop=')), None)
        f['crop'] = [int(v) for v in crop.split(',')] if crop else [0, 56, 256, 176 if f['rows'] else 232]
        f['png'] = next((int(c[4:]) for c in chk if c.startswith('png=')), f['snaps'][len(f['snaps']) // 2] if f['snaps'] else None)
        f['coverage'] = f['name'].startswith('cov_')
        out.append(f)
    return out


def pick(fs, names):
    if not names:
        return fs
    out = []
    for f in fs:
        if any(f['name'] == n or f['name'].startswith(n.rstrip('*')) and n.endswith('*') or (n == 'coverage' and f['coverage'])
               or (n == 'features' and not f['coverage']) for n in names):
            out.append(f)
    return out


# --- pictures: 6-bit colour (both players' palettes give the same 6-bit values), hashes per row/column
def load_rgb(path):
    from PIL import Image
    import numpy as np
    return (np.asarray(Image.open(path).convert('RGB')) >> 2).astype('uint8')


def crop_of(a, f, ours):
    x0, y0, x1, y1 = f['crop']
    d = f['rows'] if ours else 0
    return a[y0 + d:y1 + d, x0:x1]


def sig(c):
    h = lambda b: '%04x' % (zlib.crc32(b) & 0xFFFF)
    return dict(hash=hashlib.md5(c.tobytes()).hexdigest()[:16], rows=''.join(h(r.tobytes()) for r in c),
                cols=''.join(h(c[:, i].tobytes()) for i in range(c.shape[1])))


def where(e, o):
    split = lambda s: [s[i:i + 4] for i in range(0, len(s), 4)]
    rr = [i for i, (a, b) in enumerate(zip(split(e['rows']), split(o['rows']))) if a != b]
    cc = [i for i, (a, b) in enumerate(zip(split(e['cols']), split(o['cols']))) if a != b]
    if not rr and not cc:
        return 'size'
    return 'rows %d-%d x %d-%d' % (min(rr or [0]), max(rr or [0]), min(cc or [0]), max(cc or [0]))


# --- our player --------------------------------------------------------------------------------------
def slot_dir(work, rel, n):
    d = os.path.join(work, 'slot%d' % n)
    os.makedirs(d, exist_ok=True)
    for fn in ('zelda.dat', 'sfx.dat', 'fonts.dat', 'qst.dat'):
        t = os.path.join(d, fn)
        if not os.path.exists(t):
            try:
                os.link(os.path.join(rel, fn), t)       # no copy: a hard link
            except OSError:
                shutil.copy(os.path.join(rel, fn), t)
    # the build's settings with the keys the replays press (A=Z B=X Start=Enter L=A R=S Ex1=Q Ex2=W, arrows)
    keys = dict(key_a=26, key_b=24, key_s=67, key_l=1, key_r=19, key_ex1=17, key_ex2=23, key_up=84, key_down=85,
                key_left=82, key_right=83)
    keys['heart_beep'] = 0      # (upstream's default: off; Hero->HealthBeep reads 0 then)
    src = os.path.join(rel, 'zc.cfg')
    lines = open(src, encoding='utf-8', errors='replace').read().splitlines() if os.path.exists(src) else ['[zeldadx]']
    lines = [l for l in lines if l.split('=')[0].strip() not in keys]
    at = next((i + 1 for i, l in enumerate(lines) if l.strip().lower() == '[zeldadx]'), len(lines))
    lines[at:at] = ['%s = %d' % kv for kv in keys.items()]
    with open(os.path.join(d, 'zc.cfg'), 'w', newline='') as fh:
        fh.write('\r\n'.join(lines) + '\r\n')
    return d


def run_ours(f, rel, work, slot, start, keep):
    d = slot_dir(work, rel, slot)
    out = os.path.join(work, 'runs', f['name'])
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    fd = os.path.join(out, 'fd')
    os.makedirs(fd)
    zcr = os.path.join(out, 'in.zcr')
    L = ['zcreplay 1', '0 seed 1'] + ['%d key %s %d' % (fr, KEYS_ZCR[k], 1 if down else 0) for fr, down, k in f['keys']]
    open(zcr, 'w', newline='\n').write('\n'.join(L + ['end %d' % f['frames']]) + '\n')
    for fn in ('allegro.log', 'spr.txt', 'smoke.txt'):
        if os.path.exists(os.path.join(d, fn)):
            os.remove(os.path.join(d, fn))
    shutil.rmtree(os.path.join(d, 'Files'), ignore_errors=True)
    empty = os.path.join(work, 'empty.txt')
    open(empty, 'w').close()
    env = dict(os.environ, SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0', SDL_JOYSTICK_HIDAPI='0', ZQ_UI_SCRIPT=empty,
               ZC_TEST_START='%d,%d,%d,%d' % (f['dmap'], f['scr'], start[0], start[1]), ZC_SCRIPT_PRINTF='1', ZC_TEST_SAVE_RELOAD='file',
               ZC_REGION_SPRLOG=os.path.join(d, 'spr.txt'))
    for k in ('ZC_TEST_KEYS', 'ZC_TEST_ITEMS', 'ZC_TEST_INIT_ITEMS', 'ALLEGRO_TRACE'):
        env.pop(k, None)
    snaps = set(f['snaps'])
    a = [os.path.join(rel, 'zplayer.exe'), '-windowed', '-standalone', QUEST, '-notitle', '-replay', zcr, '-unthrottled',
         '-quest-smoke', os.path.join(d, 'smoke.txt')]
    if snaps:
        a += ['-framedump', fd, str(min(snaps)), str(max(snaps))]
    p = subprocess.Popen(a, cwd=d, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    keepset = set('frame%06d.bmp' % s for s in snaps)

    def prune():       # only the compared frames stay on the disk
        for q in glob.glob(os.path.join(fd, 'frame*.bmp')):
            if os.path.basename(q) not in keepset:
                try:
                    os.remove(q)
                except OSError:
                    pass
    t0 = time.time()
    while p.poll() is None:
        prune()
        time.sleep(0.05)
        if time.time() - t0 > 600:
            p.kill()
    prune()
    res = dict(exit=p.returncode, lines=[], unhandled=[], hero={}, snaps={})
    log = os.path.join(d, 'allegro.log')
    if os.path.exists(log):
        for l in open(log, encoding='utf-8', errors='replace'):
            l = l.rstrip('\r\n')
            if l.startswith('printf: '):
                l = l[8:]
                if LINE_RE.match(l):
                    res['lines'].append(l.rstrip())
            elif 'ZASM: unhandled' in l:
                res['unhandled'].append(l.strip())
        shutil.copy(log, os.path.join(out, 'allegro.log')) if keep else None
    spr = os.path.join(d, 'spr.txt')
    if os.path.exists(spr):
        for l in open(spr, encoding='utf-8', errors='replace'):
            m = re.match(r'(\d+) scr \w+ hero (-?\d+) (-?\d+)', l)
            if m:
                res['hero'][int(m.group(1))] = (int(m.group(2)), int(m.group(3)))
    for s in snaps:
        q = os.path.join(fd, 'frame%06d.bmp' % s)
        if os.path.exists(q):
            res['snaps'][s] = q
    open(os.path.join(out, 'lines.txt'), 'w', newline='\n').write('\n'.join(res['lines']) + '\n')
    return res


def compare(f, exp, res, work):
    import numpy as np
    notes = []; ok = True
    if res['exit'] not in (0, None) and not res['hero'] and not res['lines']:
        return 'FAILED', 'player exit %s' % res['exit'], None
    if f['hero'] and exp.get('hero'):
        ev = {fr: (x, y) for fr, x, y in exp['hero']}
        cur = None; n = 0; bad = []
        lo, hi = f['hero_range'] or (0, exp['frames'] - 1)
        for fr in range(exp['frames']):
            if fr in ev:
                cur = ev[fr]
            if cur is None or fr not in res['hero'] or not lo <= fr <= hi:
                continue
            n += 1
            if res['hero'][fr] != cur:
                bad.append((fr, cur, res['hero'][fr]))
        if bad:
            ok = False
            b = bad[0]
            near = all(abs(o[0] - u[0]) <= 1 and abs(o[1] - u[1]) <= 1 for _, u, o in bad)
            notes.append('hero %d/%d frames%s (first %d: up %d,%d ours %d,%d)' % (len(bad), n, ' within 1 px' if near else '', b[0], b[1][0], b[1][1], b[2][0], b[2][1]))
    item = None
    if f['lines']:
        e, o = exp.get('lines', []), res['lines']
        if f['coverage']:
            item = cov_compare(e, o, res['unhandled'])
            if item['diff'] or item['missing'] or item['unhandled']:
                ok = False
                notes.append('%d/%d as upstream; unhandled %d%s; differ %d%s%s' % (
                    item['total'] - len(item['diff']) - len(item['missing']) - len(item['unhandled']), item['total'],
                    len(item['unhandled']), ' (%s)' % ' '.join(item['unhandled'][:4]) if item['unhandled'] else '',
                    len(item['diff']), ' (%s)' % ' '.join(item['diff'][:4]) if item['diff'] else '',
                    '; not reached %d' % len(item['missing']) if item['missing'] else ''))
        elif e != o:
            ok = False
            k = next((i for i in range(min(len(e), len(o))) if e[i] != o[i]), min(len(e), len(o)))
            nd = sum(1 for i in range(max(len(e), len(o))) if i >= len(e) or i >= len(o) or e[i] != o[i])
            # which values differ: the word before each differing number, over all lines
            words = {}
            for i in range(min(len(e), len(o))):
                a, b = e[i].split(), o[i].split()
                if a != b and len(a) == len(b):
                    for j in range(len(a)):
                        if a[j] != b[j]:
                            w = a[j - 1] if j and not re.match(r'^-?[\d,.]+$', a[j - 1]) else a[j - 2] if j > 1 else '?'
                            words[w] = words.get(w, 0) + 1
            top = ', '.join('%s %d' % kv for kv in sorted(words.items(), key=lambda kv: -kv[1])[:4])
            fa, fb = (e[k].split() if k < len(e) else []), (o[k].split() if k < len(o) else [])
            j = next((i for i in range(min(len(fa), len(fb))) if fa[i] != fb[i]), 0)
            notes.append('lines %d/%d differ%s (first #%d at "%s": up "%s" ours "%s")%s' % (
                nd, len(e), ' [%s]' % top if top else '', k + 1, ' '.join(fa[:3]), ' '.join(fa[j:j + 2]), ' '.join(fb[j:j + 2]),
                '' if len(e) == len(o) else '; %d lines up, %d ours' % (len(e), len(o))))
        if not e:
            notes.append('no upstream lines')
    for s in f['snaps']:
        es = exp.get('snaps', {}).get(str(s))
        if not es:
            continue
        if s not in res['snaps']:
            ok = False; notes.append('frame %d not dumped' % s); continue
        c = crop_of(load_rgb(res['snaps'][s]), f, True)
        o = sig(c)
        if o['hash'] != es['hash']:
            ok = False
            full = load_rgb(res['snaps'][s]); x0, y0, x1, y1 = f['crop']
            shift = next((k for k in range(0, full.shape[0] - y1 + 1) if k != f['rows'] and sig(full[y0 + k:y1 + k, x0:x1])['hash'] == es['hash']), None)
            notes.append('frame %d %s' % (s, 'same picture with rows=%d' % shift if shift is not None else where(es, o)))
        if s == f['png'] and o['hash'] != es['hash'] and os.path.exists(os.path.join(EXP, f['name'] + '.png')):
            from PIL import Image
            u = load_rgb(os.path.join(EXP, f['name'] + '.png'))
            if u.shape == c.shape:
                dif = (u != c).any(axis=2)
                sheet = np.concatenate([c, u, np.where(dif[..., None], 63, 0).repeat(3, axis=2).astype('uint8')], axis=1)
                Image.fromarray((sheet << 2).astype('uint8')).save(os.path.join(work, 'runs', f['name'], 'sheet_%d.png' % s))
    if res['unhandled'] and not f['coverage']:
        notes.append('%d unhandled' % len(set(res['unhandled'])))
    return ('SAME' if ok else 'DIFF~' if f['random'] else 'DIFF'), '; '.join(notes), item


ZASM_NAMES = None


def zasm_names():
    """ZASM command / register numbers -> names, from our compiler's copy of upstream's defines.h."""
    global ZASM_NAMES
    if ZASM_NAMES is None:
        # built whole before it is published: worker threads must never see a half-filled table
        # (they did with --jobs 6, and unhandled items were then counted as "differ")
        names = {'cmd': {}, 'reg': {}}
        t = open(os.path.join(ROOT, 'src', 'port', 'zscript30', 'components', 'zasm', 'defines.h'), encoding='utf-8', errors='replace').read()
        for m in re.finditer(r'^\s*(\w+),\s*//\s*(0x[0-9A-Fa-f]+)', t, re.M):
            names['cmd'][int(m.group(2), 16)] = m.group(1)
        for m in re.finditer(r'^#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+)\b', t, re.M):
            names['reg'].setdefault(int(m.group(2), 0), m.group(1))
        ZASM_NAMES = names
    return ZASM_NAMES


def cov_compare(e, o, unhandled):
    def items(ls):
        d = {}
        for l in ls:
            m = re.match(r'C: (\S+) (.*)', l)
            if m and m.group(1) != 'DONE':
                d[m.group(1)] = m.group(2)
        return d
    ue, uo = items(e), items(o)
    names = zasm_names(); un = set()
    for l in unhandled:
        m = re.search(r'unhandled (register \w+|command) (\d+)', l)
        if m:
            un.add(names['reg' if m.group(1).startswith('register') else 'cmd'].get(int(m.group(2)), '?%s' % m.group(2)))
    unh = [k for k in ue if k in un]
    diff = [k for k in ue if k in uo and ue[k] != uo[k] and k not in un]
    missing = [k for k in ue if k not in uo and k not in un]
    return dict(total=len(ue), diff=diff, missing=missing, unhandled=unh)


# --- upstream (record once) --------------------------------------------------------------------------
def up_farm(up, work, n):
    """A folder of hard links to upstream's program files (no copies), so each run has its own allegro.log."""
    d = os.path.join(work, 'up%d' % n)
    if os.path.exists(os.path.join(d, 'zplayer.exe')):
        return d
    skip = {'CMakeFiles', '_deps', 'src', 'third_party', 'generated', 'packaging', 'docs', 'quests', 'tilesets', 'music', 'tmp2'}
    for root, dirs, files in os.walk(up):
        rp = os.path.relpath(root, up)
        if rp != '.' and rp.split(os.sep)[0] in skip:
            continue
        os.makedirs(os.path.join(d, rp), exist_ok=True)
        for fn in files:
            if fn.endswith(('.pdb', '.lib', '.obj', '.ilk', '.ninja', '.sav')) or fn in ('allegro.log',):
                continue
            t = os.path.join(d, rp, fn)
            if not os.path.exists(t):
                if fn.endswith('.cfg'):
                    shutil.copy(os.path.join(root, fn), t)
                else:
                    os.link(os.path.join(root, fn), t)
    return d


def record_one(f, up, work, slot):
    from PIL import Image
    farm = up_farm(up, work, slot)
    out = os.path.join(work, 'up_runs', f['name'])
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    z = os.path.join(out, 'in.zplay')
    L = ['M version latest', 'M qst ' + QUEST.replace('\\', '/'), 'M test_mode true', 'M debug true',
         'M starting_dmap %d' % f['dmap'], 'M starting_scr %d' % f['scr'], 'M starting_retsqr 0', 'K 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0']
    L += ['%s %d %s' % ('D' if down else 'U', fr, KEYS_UP[k]) for fr, down, k in f['keys']]
    open(z, 'w', newline='\n').write('\n'.join(L + ['Q %d 3' % f['frames']]) + '\n')
    log = os.path.join(farm, 'allegro.log')
    at = os.path.getsize(log) if os.path.exists(log) else 0
    a = [os.path.join(farm, 'zplayer.exe'), '-headless', '-update', z, '-replay-output-dir', out, '-replay-exit-when-done']
    if f['snaps']:
        a[4:4] = ['-snapshot', ' '.join(str(s) for s in f['snaps'])]
    r = subprocess.run(a, cwd=farm, capture_output=True, text=True, timeout=1200, creationflags=NOWIN)
    exp = dict(name=f['name'], dmap=f['dmap'], scr=f['scr'], frames=f['frames'], crop=f['crop'], rows=f['rows'],
               recorded='upstream 3.0 build-3.0 (ef321f8) headless, ' + time.strftime('%Y-%m-%d'), hero=[], lines=[], snaps={})
    for l in open(z, encoding='utf-8', errors='replace'):
        m = re.match(r'C (\d+) h ([0-9a-f]+) ([0-9a-f]+)', l)
        if m:
            exp['hero'].append([int(m.group(1)), int(m.group(2), 16), int(m.group(3), 16)])
    if exp['hero']:
        exp['start'] = [exp['hero'][0][1] % 256, exp['hero'][0][2] % 176]
    if os.path.exists(log):
        with open(log, 'rb') as fh:
            fh.seek(at)
            for l in fh.read().decode('utf-8', 'replace').split('\n'):
                l = re.sub(r'^[A-Za-z ]+\([^)]*\): ', '', l.rstrip('\r'))
                if LINE_RE.match(l):
                    exp['lines'].append(l.rstrip())
    for s in f['snaps']:
        p = os.path.join(out, 'in.zplay.%d.png' % s)
        if os.path.exists(p):
            c = crop_of(load_rgb(p), f, False)
            exp['snaps'][str(s)] = sig(c)
            if s == f['png']:
                Image.fromarray((c << 2).astype('uint8')).save(os.path.join(EXP, f['name'] + '.png'), optimize=True)
    json.dump(exp, open(os.path.join(EXP, f['name'] + '.json'), 'w'), separators=(',', ':'))
    if not os.environ.get('SUITE_KEEP'):
        shutil.rmtree(out, ignore_errors=True)
    return '%-18s exit %s, hero %d, lines %d, snaps %d/%d' % (f['name'], r.returncode, len(exp['hero']), len(exp['lines']),
                                                              len(exp['snaps']), len(f['snaps']))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('names', nargs='*')
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--build', default=os.path.join(ROOT, 'build', 'Release'))
    ap.add_argument('--work', default=os.path.join(os.environ.get('TEMP', '/tmp'), 'zc_feature_suite'))
    ap.add_argument('--upstream', default=None)
    ap.add_argument('--keep', action='store_true')
    a = ap.parse_args()
    fs = features()
    cmd = a.names[0] if a.names and a.names[0] in ('list', 'record', 'run') else 'run'
    names = a.names[1:] if a.names and a.names[0] in ('list', 'record', 'run') else a.names
    sel = pick(fs, names)
    os.makedirs(a.work, exist_ok=True)
    if cmd == 'list':
        for f in sel:
            print('%-18s dmap %3d scr %3d  %s' % (f['name'], f['dmap'], f['scr'], f['what']))
        return 0
    if cmd == 'record':
        if not a.upstream:
            print('record needs --upstream <upstream 3.0 build folder>')
            return 2
        os.makedirs(EXP, exist_ok=True)
        jobs = max(1, min(a.jobs, 4))
        lock = threading.Lock(); free = list(range(jobs))

        def rec(f):     # each run has a folder of its own while it runs (its allegro.log)
            with lock:
                s = free.pop()
            try:
                return record_one(f, a.upstream, a.work, s)
            finally:
                with lock:
                    free.append(s)
        with ThreadPoolExecutor(jobs) as ex:
            for r in ex.map(rec, sel):
                print(r, flush=True)
        if not os.environ.get('SUITE_KEEP'):
            for i in range(jobs):
                shutil.rmtree(os.path.join(a.work, 'up%d' % i), ignore_errors=True)
        return 0
    rel = os.path.abspath(a.build)
    if not os.path.exists(os.path.join(rel, 'zplayer.exe')):
        print('no zplayer.exe in', rel)
        return 2
    lock = threading.Lock(); free = list(range(a.jobs)); results = {}

    def one(f):
        ep = os.path.join(EXP, f['name'] + '.json')
        if not os.path.exists(ep):
            return f, 'MISSING', 'no expected data (record it)', None
        exp = json.load(open(ep))
        with lock:
            s = free.pop()
        try:
            res = run_ours(f, rel, a.work, s, exp.get('start', [0, 0]), a.keep)
            st, note, item = compare(f, exp, res, a.work)
        finally:
            with lock:
                free.append(s)
        if not a.keep:
            shutil.rmtree(os.path.join(a.work, 'runs', f['name'], 'fd'), ignore_errors=True)
        return f, st, note, item
    t0 = time.time(); cnt = {}; cov = [0, 0, 0, 0]; covd = []
    zasm_names()        # fill the name table before the worker threads start
    with ThreadPoolExecutor(a.jobs) as ex:
        for f, st, note, item in ex.map(one, sel):
            cnt[st] = cnt.get(st, 0) + 1
            if item:
                cov[0] += item['total']; cov[1] += item['total'] - len(item['diff']) - len(item['missing']) - len(item['unhandled'])
                cov[2] += len(item['diff']); cov[3] += len(item['unhandled']); covd.append((f['name'], item))
            print('%-20s %-6s %s' % (f['name'], st, note), flush=True)
    print('total: %d features, %s (%.0f s; DIFF~ = differs where the scene is random)' % (len(sel), ', '.join('%s %d' % kv for kv in sorted(cnt.items())), time.time() - t0))
    if cov[0]:
        print('coverage: %d of %d script commands/registers as upstream; %d unhandled in our player, %d give other values, %d not reached'
              % (cov[1], cov[0], cov[3], cov[2], cov[0] - cov[1] - cov[2] - cov[3]))
        with open(os.path.join(a.work, 'coverage.txt'), 'w') as fh:
            for n, it in covd:
                for k in it['unhandled']: print('%s unhandled %s' % (n, k), file=fh)
                for k in it['diff']: print('%s differs %s' % (n, k), file=fh)
                for k in it['missing']: print('%s not-reached %s' % (n, k), file=fh)
        print('coverage list: %s' % os.path.join(a.work, 'coverage.txt'))
    print('details: %s' % os.path.join(a.work, 'runs'))
    return 0 if cnt.get('SAME', 0) == len(sel) else 1


if __name__ == '__main__':
    sys.exit(main())

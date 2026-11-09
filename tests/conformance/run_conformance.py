#!/usr/bin/env python3
"""
run_conformance.py - compare the reference (Allegro 4.4) and port builds of
the conformance programs.

Builds nothing. For every program pair found in <build-dir>/conformance/<config>/
(conf_<name>_ref.exe / conf_<name>_port.exe) it runs both sides with the same
arguments, compares their check lines ("test/case: value") and prints a
PASS/FAIL table. For a failing test it re-runs that test on both sides with
--expand and --dump and shows the first differing lines, items and pixels.

    python -I tests/conformance/run_conformance.py <build-dir> [options]

Exit status: 0 everything passed or was skipped, 1 something failed,
2 setup problem (no programs found, bad arguments).
"""

import argparse
import fnmatch
import os
import re
import shutil
import subprocess
import sys
import time
from collections import OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DEFAULT_DATA = os.path.join(ROOT, 'build', 'Release')
DEFAULT_QUESTS = os.path.normpath(os.path.join(ROOT, '..', 'quests'))
EXE_SUFFIX = '.exe' if os.name == 'nt' else ''
PROG_RE = re.compile(r'^conf_(.+)_(ref|port)' + re.escape(EXE_SUFFIX) + r'$', re.I)

KNOWN_CODES = {
    0xC0000005: 'access violation',
    0xC00000FD: 'stack overflow',
    0xC0000094: 'integer divide by zero',
    0xC0000409: 'stack buffer overrun / fast fail',
    0xC000001D: 'illegal instruction',
    0xC000000D: 'CRT invalid parameter',
    0xC0000025: 'pure virtual call',
    0xC0000135: 'DLL not found (alleg44.dll missing?)',
    0xC0000139: 'DLL entry point not found',
    0xC000007B: 'bad executable image (32/64-bit mix?)',
    3: 'abort()',
    4: 'install_allegro failed',
    5: 'Allegro ASSERT',
    2: 'bad arguments',
}


def describe_code(code):
    if code is None:
        return 'not run'
    c = code & 0xFFFFFFFF
    text = ('0x%08X' % c) if c > 255 else str(c)
    name = KNOWN_CODES.get(c)
    return '%s (%s)' % (text, name) if name else text


# --------------------------------------------------------------------------
# FNV-1a 64 and dump names (must match conf.cpp)
# --------------------------------------------------------------------------

def fnv1a64(data):
    h = 14695981039346656037
    for b in data:
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def dump_name(key):
    out = []
    for ch in key:
        if ch.isascii() and (ch.isalnum() or ch in '_-.'):
            out.append(ch)
        elif ch == '/':
            out.append('__')
        else:
            out.append('_')
    r = ''.join(out)
    if len(r) > 150:
        r = r[:130] + '_' + '%016x' % fnv1a64(key.encode('latin-1', 'replace'))
    return r


# --------------------------------------------------------------------------
# Output parsing
# --------------------------------------------------------------------------

class SideRun:
    """Everything one side printed (possibly merged from several runs)."""

    def __init__(self, side):
        self.side = side
        self.entries = []          # [(key, value)]
        self.finished = set()      # tests that printed "# end <name>"
        self.crash_notes = []
        # one record per process: label is the test name for isolated runs,
        # 'all' for a run of every selected test
        self.runs = []             # [(label, exit code or None, timed_out, stderr tail)]

    def merge(self, other):
        self.entries += other.entries
        self.finished |= other.finished
        self.crash_notes += other.crash_notes
        self.runs += other.runs

    def bad_exit(self):
        """(label, code, timed_out, stderr) of the first process that failed."""
        for r in self.runs:
            if r[2] or r[1] != 0:
                return r
        return None

    def exit_for(self, test):
        """The failed process that ran this test, if any."""
        for label in (test, 'all'):
            for r in self.runs:
                if r[0] == label and (r[2] or r[1] != 0):
                    return r
        return None


END_RE = re.compile(r'^# end (\S+) checks=')


def parse_output(path, run):
    if not os.path.exists(path):
        return
    with open(path, encoding='utf-8', errors='replace') as f:
        for raw in f:
            line = raw.rstrip('\r\n')
            if not line:
                continue
            if line.startswith('#'):
                m = END_RE.match(line)
                if m:
                    run.finished.add(m.group(1))
                elif line.startswith('# CRASH'):
                    run.crash_notes.append(line)
                continue
            key, sep, value = line.partition(': ')
            if not sep:
                key, value = (line[:-1], '') if line.endswith(':') else (line, '')
            run.entries.append((key, value))


def keyed(entries):
    d = OrderedDict()
    seen = {}
    for k, v in entries:
        n = seen.get(k, 0) + 1
        seen[k] = n
        d[k if n == 1 else '%s#%d' % (k, n)] = v
    return d


def test_of(key):
    return key.split('/', 1)[0]


# --------------------------------------------------------------------------
# Running programs
# --------------------------------------------------------------------------

def run_exe(exe, args, timeout, cwd=None):
    """Returns (exit code or None, timed_out, stdout, stderr)."""
    try:
        p = subprocess.run([exe] + args, cwd=cwd, stdin=subprocess.DEVNULL,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
        return p.returncode, False, p.stdout.decode('utf-8', 'replace'), p.stderr.decode('utf-8', 'replace')
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b'').decode('utf-8', 'replace')
        err = (e.stderr or b'').decode('utf-8', 'replace')
        return None, True, out, err
    except OSError as e:
        return -1, False, '', str(e)


def run_side(exe, side, prog_args, work, label, opts, extra=None):
    """Runs one side once; output and scratch files go under work/."""
    run = SideRun(side)
    os.makedirs(work, exist_ok=True)
    out_file = os.path.join(work, side + '.txt')
    tmp = os.path.join(work, side + '_tmp')
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp, exist_ok=True)
    if os.path.exists(out_file):
        os.remove(out_file)
    args = ['--quiet', '--out', out_file, '--tmp', tmp] + prog_args + (extra or [])
    code, timed_out, out, err = run_exe(exe, args, opts.timeout, cwd=tmp)
    with open(os.path.join(work, side + '.stdout.txt'), 'w', encoding='utf-8') as f:
        f.write(out)
    with open(os.path.join(work, side + '.stderr.txt'), 'w', encoding='utf-8') as f:
        f.write(err)
    parse_output(out_file, run)
    err_lines = [l for l in err.strip().splitlines() if l.strip() and not l.startswith('# CRASH')]
    run.runs.append((label, code, timed_out, ' | '.join(err_lines[-5:])))
    if opts.verbose:
        print('    ran %s: %s' % (side, describe_code(code) if not timed_out else 'TIMEOUT'))
    return run


def list_tests(exe, prog_args, opts):
    """Names the program would run with these args, or (None, error)."""
    code, timed_out, out, err = run_exe(exe, ['--list'] + prog_args, 60)
    if timed_out or code != 0:
        return None, 'listing tests failed: %s %s' % (
            'TIMEOUT' if timed_out else describe_code(code), err.strip()[:200])
    return [l.strip() for l in out.splitlines() if l.strip()], None


# --------------------------------------------------------------------------
# Comparison
# --------------------------------------------------------------------------

class TestResult:
    def __init__(self, prog, test):
        self.prog = prog
        self.test = test
        self.checks = 0
        self.diffs = []        # [(key, ref value or None, port value or None)]
        self.fails = []        # [(side, key, value)]
        self.skips = []
        self.problems = []     # strings
        self.status = 'PASS'

    def finish(self):
        if self.problems or self.diffs or self.fails:
            self.status = 'FAIL'
        elif self.skips:
            self.status = 'SKIP'
        return self


def jitter_problems(first, second, results):
    """Adds a problem to each test whose output differs between a normal run
    and a --jitter run of the same side (at most 3 lines per test)."""
    a, b = keyed(first.entries), keyed(second.entries)
    shown = {}
    for k in list(a.keys()) + [k for k in b if k not in a]:
        if a.get(k) == b.get(k) or test_of(k) not in results:
            continue
        r = results[test_of(k)]
        shown[r.test] = shown.get(r.test, 0) + 1
        if shown[r.test] <= 3:
            r.problems.append('%s output changes with --jitter (reads undefined memory?): %s: %s -> %s'
                              % (first.side, k, clip(a.get(k), 60), clip(b.get(k), 60)))


def flag_values(rec, side, key, value):
    if value.startswith('FAIL'):
        rec.fails.append((side, key, value))
    elif value.startswith('SKIP'):
        rec.skips.append(value)


def compare_program(prog, names, ref, port, portonly):
    results = OrderedDict((n, TestResult(prog, n)) for n in names)

    def rec(test):
        if test not in results:
            results[test] = TestResult(prog, test)
        return results[test]

    if portonly:
        for k, v in port.entries:
            r = rec(test_of(k))
            r.checks += 1
            flag_values(r, 'port', k, v)
    else:
        rmap = keyed(ref.entries)
        pmap = keyed(port.entries)
        for k, v in rmap.items():
            r = rec(test_of(k))
            r.checks += 1
            flag_values(r, 'ref', k, v)
            pv = pmap.get(k)
            if pv is None:
                r.diffs.append((k, v, None))
            else:
                flag_values(r, 'port', k, pv)
                if pv != v:
                    r.diffs.append((k, v, pv))
        for k, v in pmap.items():
            if k not in rmap:
                r = rec(test_of(k))
                r.diffs.append((k, None, v))
                flag_values(r, 'port', k, v)

    sides = [port] if portonly else [ref, port]
    for name, r in results.items():
        for s in sides:
            if name not in s.finished:
                bad = s.exit_for(name)
                why = 'did not finish'
                if bad:
                    why += ' (%s)' % ('TIMEOUT' if bad[2] else 'exit ' + describe_code(bad[1]))
                if s.side == 'ref' and not portonly:
                    why += ' -- the reference itself fails: fix the test'
                r.problems.append('%s %s' % (s.side, why))
        r.finish()
    return results


# --------------------------------------------------------------------------
# Failure details (dump analysis)
# --------------------------------------------------------------------------

def read_bmp_dump(path):
    with open(path, 'rb') as f:
        data = f.read()
    nl = data.find(b'\n')
    head = data[:nl].decode('ascii', 'replace').split()
    if len(head) != 4 or head[0] != 'CONFBMP':
        return None
    w, h, depth = int(head[1]), int(head[2]), int(head[3])
    return w, h, depth, data[nl + 1:]


def bpp_of(depth):
    return {8: 1, 15: 2, 16: 2, 24: 3, 32: 4}.get(depth, 0)


def describe_bmp_diff(rp, pp, limit=8):
    r = read_bmp_dump(rp)
    p = read_bmp_dump(pp)
    if not r or not p:
        return ['(unreadable bitmap dump)']
    rw, rh, rd, rdat = r
    pw, ph, pd, pdat = p
    if (rw, rh, rd) != (pw, ph, pd):
        return ['bitmap shape differs: ref %dx%dx%d, port %dx%dx%d' % (rw, rh, rd, pw, ph, pd)]
    bpp = bpp_of(rd)
    if bpp == 0:
        return ['unknown color depth %d' % rd]
    diffs = []
    n = 0
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    for y in range(rh):
        off = y * rw * bpp
        rrow = rdat[off:off + rw * bpp]
        prow = pdat[off:off + rw * bpp]
        if rrow == prow:
            continue
        for x in range(rw):
            a = rrow[x * bpp:(x + 1) * bpp]
            b = prow[x * bpp:(x + 1) * bpp]
            if a != b:
                n += 1
                x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
                if len(diffs) < limit:
                    av = int.from_bytes(a, 'little')
                    bv = int.from_bytes(b, 'little')
                    diffs.append('(%d,%d) ref=%s port=%s' % (
                        x, y, av if bpp == 1 else '0x%0*x' % (bpp * 2, av),
                        bv if bpp == 1 else '0x%0*x' % (bpp * 2, bv)))
    if n == 0:
        return ['bitmap dumps are identical (difference is elsewhere in the value)']
    lines = ['%d of %d pixels differ, in x %d..%d, y %d..%d' % (n, rw * rh, x0, x1, y0, y1)]
    lines.append('first: ' + '; '.join(diffs))
    return lines


def describe_mem_diff(rp, pp):
    with open(rp, 'rb') as f:
        a = f.read()
    with open(pp, 'rb') as f:
        b = f.read()
    lines = []
    if len(a) != len(b):
        lines.append('size differs: ref %d bytes, port %d bytes' % (len(a), len(b)))
    n = min(len(a), len(b))
    first = next((i for i in range(n) if a[i] != b[i]), None)
    if first is None:
        if len(a) == len(b):
            return ['dumps are identical']
        first = n
    count = sum(1 for i in range(n) if a[i] != b[i])
    lines.append('%d differing bytes in the common %d; first at offset %d (0x%x)' % (count, n, first, first))
    s = max(0, first - 4)
    lines.append('ref  @%06x: %s' % (s, a[s:s + 16].hex(' ')))
    lines.append('port @%06x: %s' % (s, b[s:s + 16].hex(' ')))
    return lines


def find_dump(dump_dir, key):
    base = os.path.join(dump_dir, dump_name(key))
    for kind in ('bmp', 'mem', 'pal'):
        if os.path.exists(base + '.' + kind):
            return base + '.' + kind, kind
    return None, None


def clip(s, n=110):
    if s is None:
        return '(missing)'
    return s if len(s) <= n else s[:n - 3] + '...'


def details_for(prog, test, exes, prog_args, work, opts):
    """Re-run one failing test with --expand/--dump; return report lines."""
    lines = []
    dwork = os.path.join(work, 'details', test)
    shutil.rmtree(dwork, ignore_errors=True)
    runs = {}
    for side in ('ref', 'port'):
        if not exes.get(side):
            continue
        dump_dir = os.path.join(dwork, side + '_dump')
        runs[side] = run_side(exes[side], side, prog_args, dwork, test, opts,
                              ['--test', test, '--expand', '--dump', dump_dir])
        runs[side].dump_dir = dump_dir
    if len(runs) < 2:
        return False, lines
    rmap = keyed(runs['ref'].entries)
    pmap = keyed(runs['port'].entries)
    keys = list(rmap.keys()) + [k for k in pmap.keys() if k not in rmap]
    shown = 0
    for k in keys:
        rv, pv = rmap.get(k), pmap.get(k)
        if rv == pv:
            continue
        if shown >= opts.max_diffs:
            lines.append('  ... more differences (use --max-diffs)')
            break
        shown += 1
        lines.append('  %s' % k)
        lines.append('    ref : %s' % clip(rv))
        lines.append('    port: %s' % clip(pv))
        if rv is not None and pv is not None:
            rp, kind = find_dump(runs['ref'].dump_dir, k)
            pp, _ = find_dump(runs['port'].dump_dir, k)
            if rp and pp:
                d = describe_bmp_diff(rp, pp) if kind == 'bmp' else describe_mem_diff(rp, pp)
                lines += ['    dump: ' + d[0]] + ['          ' + x for x in d[1:]]
    if shown == 0:
        lines.append('  (no difference when the test runs alone with --expand: it depends on an earlier test)')
    for side in ('ref', 'port'):
        lines.append('  repro (%s)%s: %s' % (side, ' ' if side == 'ref' else '', subprocess.list2cmdline(
            [exes[side], '--test', test, '--expand'] + prog_args)))
    lines.append('  outputs, dumps: ' + dwork)
    return shown > 0, lines


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def find_programs(build_dir, config):
    out_dir = os.path.join(build_dir, 'conformance', config)
    dirs = []
    if os.path.isdir(out_dir):
        dirs.append(out_dir)
    else:
        for root, subdirs, files in os.walk(build_dir):
            subdirs[:] = [d for d in subdirs if d.lower() not in ('third_party', 'cmakefiles')]
            if os.path.basename(root).lower() == config.lower() and any(PROG_RE.match(f) for f in files):
                dirs.append(root)
    progs = OrderedDict()
    for d in dirs:
        for f in sorted(os.listdir(d)):
            m = PROG_RE.match(f)
            if m:
                progs.setdefault(m.group(1), {})[m.group(2).lower()] = os.path.join(d, f)
    return dirs, progs


def select(progs, specs):
    """specs: 'prog', 'prog/test', or a bare test name; globs allowed."""
    if not specs:
        return [(p, None) for p in progs]
    chosen = OrderedDict()
    for spec in specs:
        spec = spec[5:] if spec.startswith('conf_') else spec
        if '/' in spec:
            ppat, test = spec.split('/', 1)
        else:
            ppat, test = spec, None
        matched = [p for p in progs if fnmatch.fnmatchcase(p, ppat)]
        if test is None and not matched:
            matched, test = list(progs), spec          # bare test name
        for p in matched:
            cur = chosen.setdefault(p, set())
            if cur is not None:
                if test is None:
                    chosen[p] = None
                else:
                    cur.add(test)
    return [(p, sorted(t) if t else None) for p, t in chosen.items()]


def main():
    ap = argparse.ArgumentParser(
        description='Run conformance programs (reference Allegro vs port) and compare outputs.')
    ap.add_argument('build_dir', help='CMake build directory (the one passed to cmake -B)')
    ap.add_argument('--config', default='Release', help='build configuration (default: Release)')
    ap.add_argument('--test', '-t', action='append', default=[], metavar='SPEC',
                    help='program name ("harness_smoke"), "program/test", or a test name; '
                         'globs allowed; repeatable (default: everything)')
    ap.add_argument('--filter', help='only tests whose name contains this (passed to the programs)')
    ap.add_argument('--data', default=DEFAULT_DATA, help='game data directory (default: %(default)s)')
    ap.add_argument('--quests', default=DEFAULT_QUESTS,
                    help='custom quests directory (default: %(default)s)')
    ap.add_argument('--system', choices=('none', 'auto'), help='override the install_allegro system')
    ap.add_argument('--timeout', type=float, default=300, help='seconds per program run (default 300)')
    ap.add_argument('--max-diffs', type=int, default=10, help='differences shown per failing test')
    ap.add_argument('--isolate', action='store_true', help='run every test in its own process')
    ap.add_argument('--no-details', action='store_true', help='do not re-run failures with --expand/--dump')
    ap.add_argument('--check-determinism', action='store_true',
                    help='also run both sides with --jitter (disturbed heap) and fail tests whose '
                         'output changes; use it on every new test')
    ap.add_argument('--ref-only', action='store_true',
                    help='run only the reference side and print its lines (while writing a test)')
    ap.add_argument('--allow-missing-port', action='store_true',
                    help='a program without a port build is NO-PORT instead of FAIL')
    ap.add_argument('--work', help='directory for outputs (default: <build>/conformance/run)')
    ap.add_argument('--list', action='store_true', help='list the programs found and exit')
    ap.add_argument('--verbose', '-v', action='store_true')
    ap.add_argument('extra', nargs='*', help='extra arguments for the programs (after --)')
    opts = ap.parse_args()

    build_dir = os.path.abspath(opts.build_dir)
    dirs, progs = find_programs(build_dir, opts.config)
    if not progs:
        print('no conformance programs (conf_*_ref%s / conf_*_port%s) under %s for config %s'
              % (EXE_SUFFIX, EXE_SUFFIX, build_dir, opts.config))
        print('build them first, e.g.: cmake --build "%s" --config %s --target conf_all'
              % (build_dir, opts.config))
        return 2

    if opts.list:
        for p, exes in progs.items():
            print('%-40s %s' % (p, ' '.join(s for s in ('ref', 'port') if s in exes)))
        return 0

    selection = select(progs, opts.test)
    work_root = os.path.abspath(opts.work or os.path.join(build_dir, 'conformance', 'run'))
    common = ['--data', opts.data, '--quests', opts.quests]
    if opts.filter:
        common += ['--filter', opts.filter]
    if opts.system:
        common += ['--system', opts.system]
    common += opts.extra

    print('conformance: %d program(s) in %s' % (len(selection), '; '.join(dirs)))
    print('data: %s   quests: %s' % (opts.data, opts.quests))
    t_start = time.time()
    all_results = []
    detail_blocks = []
    exit_status = 0

    for prog, tests in selection:
        exes = progs[prog]
        portonly = prog.endswith('_portonly')
        work = os.path.join(work_root, prog)
        shutil.rmtree(work, ignore_errors=True)

        lister = exes.get('ref') or exes.get('port')
        names, err = list_tests(lister, common, opts)
        if names is None:
            r = TestResult(prog, '(program)')
            r.problems.append(err)
            all_results.append(r.finish())
            continue
        sel_args = []
        if tests:
            names = [n for n in names if n in tests]
            if not names:
                continue                     # this program has none of the named tests
            for n in names:
                sel_args += ['--test', n]

        if opts.ref_only:
            if not exes.get('ref'):
                continue
            run = run_side(exes['ref'], 'ref', common + sel_args, work, 'all', opts)
            print('---- %s (reference only)' % prog)
            for k, v in run.entries:
                print('%s: %s' % (k, v))
            jittered = None
            if opts.check_determinism:
                jittered = run_side(exes['ref'], 'ref', common + sel_args + ['--jitter', '7919'],
                                    os.path.join(work, 'jitter'), 'all', opts)
            ref_results = OrderedDict()
            for name in names:
                r = TestResult(prog, name)
                ref_results[name] = r
                r.checks = sum(1 for k, _ in run.entries if test_of(k) == name)
                for k, v in run.entries:
                    if test_of(k) == name:
                        flag_values(r, 'ref', k, v)
                if name not in run.finished:
                    bad = run.exit_for(name)
                    r.problems.append('ref did not finish' + (
                        ' (exit %s)' % describe_code(bad[1]) if bad and not bad[2] else
                        ' (TIMEOUT)' if bad else ''))
            if jittered:
                jitter_problems(run, jittered, ref_results)
            for r in ref_results.values():
                r.finish()
                if r.status == 'PASS':
                    r.status = 'REF-OK'
                all_results.append(r)
                if r.status == 'FAIL':
                    detail_blocks.append(['---- FAIL %s/%s (reference only)' % (prog, r.test)] +
                                         ['  ' + p for p in r.problems] +
                                         ['  ref reports %s: %s' % (k, clip(v)) for _, k, v in r.fails])
            continue

        missing = [s for s in (('port',) if portonly else ('ref', 'port')) if not exes.get(s)]
        if missing:
            for name in names:
                r = TestResult(prog, name)
                if missing == ['port'] and opts.allow_missing_port:
                    r.status = 'NO-PORT'
                else:
                    r.problems.append('no %s program built' % ' or '.join(missing))
                    r.finish()
                all_results.append(r)
            continue

        def run_all(isolated, extra=None, subdir=''):
            ref_run, port_run = SideRun('ref'), SideRun('port')
            groups = names if isolated else [None]
            for g in groups:
                sub = os.path.join(work, subdir, g) if g else os.path.join(work, subdir)
                args = common + (['--test', g] if g else sel_args) + (extra or [])
                label = g or 'all'
                if not portonly:
                    ref_run.merge(run_side(exes['ref'], 'ref', args, sub, label, opts))
                port_run.merge(run_side(exes['port'], 'port', args, sub, label, opts))
            return ref_run, port_run

        isolated = opts.isolate
        ref_run, port_run = run_all(isolated)
        crashed = [s for s in ((port_run,) if portonly else (ref_run, port_run)) if s.bad_exit()]
        isolated_note = None
        if crashed and not isolated and len(names) > 1:
            isolated_note = '%s exited with %s; re-ran each test in its own process' % (
                ' and '.join(s.side for s in crashed),
                ', '.join('TIMEOUT' if s.bad_exit()[2] else describe_code(s.bad_exit()[1]) for s in crashed))
            isolated = True
            ref_run, port_run = run_all(True)

        results = compare_program(prog, names, ref_run, port_run, portonly)

        if opts.check_determinism:
            # same programs again with a disturbed heap: any change means a
            # test (or the port) reads memory it does not own
            jref, jport = run_all(isolated, ['--jitter', '7919'], 'jitter')
            for first, second in ((ref_run, jref), (port_run, jport)):
                if portonly and first.side == 'ref':
                    continue
                jitter_problems(first, second, results)
            for r in results.values():
                r.finish()

        # a crash after the last test (e.g. in allegro_exit) still fails the program
        for s in ((port_run,) if portonly else (ref_run, port_run)):
            bad = s.bad_exit()
            if bad and all(n in s.finished for n in names):
                r = TestResult(prog, '(exit)')
                r.problems.append('%s exited with %s after its tests' % (
                    s.side, 'TIMEOUT' if bad[2] else describe_code(bad[1])))
                results['(exit)'] = r.finish()

        for name, r in results.items():
            all_results.append(r)
            if r.status != 'FAIL':
                continue
            block = ['---- FAIL %s/%s' % (prog, name)]
            if isolated_note:
                block.append('  note: ' + isolated_note)
            for p in r.problems:
                block.append('  ' + p)
            for s in (ref_run, port_run):
                for c in s.crash_notes:
                    if ("'%s'" % name) in c:
                        block.append('  %s: %s' % (s.side, c[2:]))
                bad = s.exit_for(name)
                if bad and bad[3] and any(p.startswith(s.side) for p in r.problems):
                    block.append('  %s stderr: %s' % (s.side, bad[3][:300]))
            for side, k, v in r.fails[:opts.max_diffs]:
                block.append('  %s reports %s: %s' % (side, k, clip(v)))
            if r.diffs:
                block.append('  %d differing line(s)' % len(r.diffs))
                found, details = False, []
                if not opts.no_details and not portonly and name not in ('(exit)', '(program)'):
                    found, details = details_for(prog, name, exes, common, work, opts)
                if found:
                    block.append('  first differences (test re-run alone with --expand --dump):')
                else:
                    for k, rv, pv in r.diffs[:opts.max_diffs]:
                        block.append('  %s' % k)
                        block.append('    ref : %s' % clip(rv))
                        block.append('    port: %s' % clip(pv))
                block += details
            detail_blocks.append(block)

    # ---- table
    print()
    pw = max([len('PROGRAM')] + [len(r.prog) for r in all_results])
    tw = max([len('TEST')] + [len(r.test) for r in all_results])
    print('%-*s  %-*s  %-7s  %6s  %s' % (pw, 'PROGRAM', tw, 'TEST', 'RESULT', 'CHECKS', 'DETAIL'))
    counts = {}
    for r in all_results:
        counts[r.status] = counts.get(r.status, 0) + 1
        if r.problems:
            detail = r.problems[0]
        elif r.diffs:
            detail = '%d line(s) differ; first: %s' % (len(r.diffs), r.diffs[0][0])
        elif r.fails:
            detail = '%s: %s' % (r.fails[0][1], clip(r.fails[0][2], 60))
        elif r.skips:
            detail = clip(r.skips[0], 60)
        else:
            detail = ''
        print('%-*s  %-*s  %-7s  %6d  %s' % (pw, r.prog, tw, r.test, r.status, r.checks, detail))
        if r.status == 'FAIL':
            exit_status = 1

    for block in detail_blocks:
        print()
        print('\n'.join(block))

    print()
    summary = ', '.join('%d %s' % (n, s) for s, n in sorted(counts.items()))
    print('summary: %s  (%.1f s)  outputs: %s' % (summary or 'nothing ran', time.time() - t_start, work_root))
    if not all_results:
        return 2
    return exit_status


if __name__ == '__main__':
    sys.exit(main())

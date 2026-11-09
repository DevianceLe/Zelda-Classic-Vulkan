#!/usr/bin/env python3
"""Compare two Zelda Classic frame-hash logs (written with -framehash).

    python -I tools/replay_compare.py expected.txt actual.txt

Reports the first frame whose picture or palette differs, how many frames
differ, and frames present in only one log. Comment lines (# ...) are
ignored for the comparison; warnings and "stopped" notes found in them are
listed. Exit status: 0 identical, 1 different, 2 bad input.

See docs/zplayer-replay.md.
"""

import argparse
import sys


def load(path):
    frames = {}
    order = []
    notes = []
    try:
        f = open(path, "r", encoding="utf-8", errors="replace")
    except OSError as e:
        sys.exit("replay_compare: cannot open %s: %s" % (path, e.strerror))
    with f:
        first = True
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                text = line[1:].strip()
                if text.startswith("warning") or text.startswith("stopped"):
                    notes.append(text)
                continue
            if first:
                first = False
                if line.split()[:2] != ["zcframehash", "1"]:
                    sys.exit("replay_compare: %s is not a frame-hash log (no 'zcframehash 1' header)" % path)
                continue
            parts = line.split()
            if len(parts) != 3:
                sys.exit("replay_compare: %s:%d: expected '<frame> <picture hash> <palette hash>'" % (path, lineno))
            try:
                n = int(parts[0])
            except ValueError:
                sys.exit("replay_compare: %s:%d: bad frame number %r" % (path, lineno, parts[0]))
            if n in frames:
                sys.exit("replay_compare: %s:%d: frame %d appears twice" % (path, lineno, n))
            frames[n] = (parts[1].lower(), parts[2].lower())
            order.append(n)
    if first:
        sys.exit("replay_compare: %s is empty" % path)
    return frames, order, notes


def ranges(nums, limit=None):
    """[1,2,3,7,9,10] -> '1-3, 7, 9-10' (at most `limit` ranges)"""
    out = []
    start = prev = None
    for n in nums:
        if start is None:
            start = prev = n
        elif n == prev + 1:
            prev = n
        else:
            out.append((start, prev))
            start = prev = n
    if start is not None:
        out.append((start, prev))
    more = limit is not None and len(out) > limit
    if more:
        out = out[:limit]
    text = ", ".join(str(a) if a == b else "%d-%d" % (a, b) for a, b in out)
    return text + (" ..." if more else "")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("expected")
    ap.add_argument("actual")
    ap.add_argument("--max-list", type=int, default=20,
                    help="frame ranges to list (default 20)")
    args = ap.parse_args()

    a, a_order, a_notes = load(args.expected)
    b, b_order, b_notes = load(args.actual)

    for name, notes in ((args.expected, a_notes), (args.actual, b_notes)):
        for t in notes:
            print("note (%s): %s" % (name, t))

    common = sorted(set(a) & set(b))
    only_a = sorted(set(a) - set(b))
    only_b = sorted(set(b) - set(a))

    diff_pic = []
    diff_pal = []
    for n in common:
        if a[n][0] != b[n][0]:
            diff_pic.append(n)
        if a[n][1] != b[n][1]:
            diff_pal.append(n)
    differing = sorted(set(diff_pic) | set(diff_pal))

    print("expected: %s (%d frames)" % (args.expected, len(a)))
    print("actual:   %s (%d frames)" % (args.actual, len(b)))

    if not differing and not only_a and not only_b:
        print("IDENTICAL: %d frames match" % len(common))
        return 0

    if differing:
        f0 = differing[0]
        what = []
        if f0 in diff_pic:
            what.append("picture")
        if f0 in diff_pal:
            what.append("palette")
        print("FIRST DIFFERENCE at frame %d (%s)" % (f0, " and ".join(what)))
        print("  expected: picture %s palette %s" % a[f0])
        print("  actual:   picture %s palette %s" % b[f0])
        print("%d of %d common frames differ (picture: %d, palette: %d)"
              % (len(differing), len(common), len(diff_pic), len(diff_pal)))
        print("differing frames: %s" % ranges(differing, args.max_list))
        # Frames after the first difference that match again (e.g. a
        # one-frame glitch versus a lasting desync).
        after = [n for n in common if n > f0]
        same_after = [n for n in after if n not in set(differing)]
        if after:
            print("after frame %d: %d frames match again, %d differ"
                  % (f0, len(same_after), len(after) - len(same_after)))
    else:
        print("all %d common frames match" % len(common))

    if only_a:
        print("only in expected: %d frames (%s)" % (len(only_a), ranges(only_a, args.max_list)))
    if only_b:
        print("only in actual: %d frames (%s)" % (len(only_b), ranges(only_b, args.max_list)))
    print("DIFFERENT")
    return 1


if __name__ == "__main__":
    sys.exit(main())

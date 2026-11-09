#!/usr/bin/env python3
"""Run a Zelda Classic player (Allegro zelda.exe or zplayer) on a recording
in a fresh, throw-away game folder, and collect its frame-hash log.

    python -I tools/replay_run.py --exe build/Release/zelda.exe \
        --data "<folder with zelda.dat, qst.dat, 1st.qst, ...>" \
        --replay tests/replay/1st_quest_walk.zcr --hashlog out.txt [--unthrottled]

Every run starts from the same state: the player and its DLLs, the game
data, a zc.cfg (windowed, see --cfg) and an EMPTY zc.sav are copied into a
new folder, so runs do not affect each other or the data folder.

--record FILE records instead of playing back (you play in the window).
Extra player switches go after "--". Exit status: the player's
(0 ok, 1 desync warnings, 2 bad switch/file, 3 stopped at a menu), or 4
if it timed out / 5 on a setup problem.

See docs/zplayer-replay.md.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

DATA_FILES = ["zelda.dat", "fonts.dat", "sfx.dat", "qst.dat",
              "1st.qst", "2nd.qst", "3rd.qst", "4th.qst", "5th.qst",
              "zelda.nsf", "Classic.nsf", "5thmusic.nsf"]
REQUIRED = ["zelda.dat", "fonts.dat", "sfx.dat", "qst.dat", "1st.qst"]

DEFAULT_CFG = "[zeldadx]\nfullscreen = 0\nwindow_display = 3\n"


def window_failed(run_dir):
    """True if the last run stopped because no window could be created."""
    try:
        with open(os.path.join(run_dir, "allegro.log"), "r", errors="replace") as f:
            return "could not create a window" in f.read()
    except OSError:
        return False


def setup(run_dir, exe, data, cfg_text, quests):
    os.makedirs(run_dir, exist_ok=True)
    exe_dir = os.path.dirname(os.path.abspath(exe))
    shutil.copy2(exe, run_dir)
    for name in os.listdir(exe_dir):
        if name.lower().endswith(".dll"):
            shutil.copy2(os.path.join(exe_dir, name), run_dir)
    # The Allegro build needs alleg44.dll; the data folder of a normal
    # install has it next to zelda.exe.
    for dll in ("alleg44.dll",):
        if not os.path.exists(os.path.join(run_dir, dll)) and os.path.exists(os.path.join(data, dll)):
            shutil.copy2(os.path.join(data, dll), run_dir)
    for name in DATA_FILES:
        src = os.path.join(data, name)
        if os.path.exists(src):
            shutil.copy2(src, run_dir)
        elif name in REQUIRED:
            raise SystemExit("replay_run: %s not found in %s" % (name, data))
    for q in quests:
        shutil.copy2(q, run_dir)
    with open(os.path.join(run_dir, "zc.cfg"), "w", newline="\n") as f:
        f.write(cfg_text)
    open(os.path.join(run_dir, "zc.sav"), "wb").close()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", required=True, help="zelda.exe or zplayer.exe")
    ap.add_argument("--data", required=True, help="folder with the game data files")
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--replay", help="recording to play back")
    mode.add_argument("--record", help="recording to write (you play)")
    ap.add_argument("--hashlog", help="where to copy the frame-hash log")
    ap.add_argument("--frames", type=int, help="stop after N frames")
    ap.add_argument("--unthrottled", action="store_true", help="run as fast as possible")
    ap.add_argument("--framedump", nargs=3, metavar=("DIR", "FIRST", "LAST"),
                    help="save frames FIRST..LAST as BMP files in DIR")
    ap.add_argument("--cfg", help="zc.cfg to use (default: windowed, window_display = 3)")
    ap.add_argument("--quest", action="append", default=[], help="extra quest file to copy (repeatable)")
    ap.add_argument("--run-dir", help="game folder to create (default: a new temporary folder)")
    ap.add_argument("--keep", action="store_true", help="keep the temporary game folder")
    ap.add_argument("--timeout", type=float, default=600, help="seconds (default 600)")
    ap.add_argument("extra", nargs="*", help="extra player switches (after --)")
    args = ap.parse_args()

    if not os.path.isfile(args.exe):
        print("replay_run: %s not found" % args.exe)
        return 5

    cfg_text = DEFAULT_CFG
    if args.cfg:
        with open(args.cfg, "r") as f:
            cfg_text = f.read()

    temp = args.run_dir is None
    run_dir = args.run_dir or tempfile.mkdtemp(prefix="zcreplay_")
    try:
        setup(run_dir, args.exe, args.data, cfg_text, args.quest)

        hash_in_run = os.path.join(run_dir, "framehash.txt")
        for stale in ("allegro.log", "framehash.txt"):
            if os.path.exists(os.path.join(run_dir, stale)):
                os.remove(os.path.join(run_dir, stale))
        cmd = [os.path.join(run_dir, os.path.basename(args.exe))]
        if args.replay:
            cmd += ["-replay", os.path.abspath(args.replay)]
        else:
            cmd += ["-record", os.path.abspath(args.record)]
        cmd += ["-framehash", hash_in_run]
        if args.frames is not None:
            cmd += ["-frames", str(args.frames)]
        if args.unthrottled:
            cmd += ["-unthrottled"]
        if args.framedump:
            d = os.path.abspath(args.framedump[0])
            os.makedirs(d, exist_ok=True)
            cmd += ["-framedump", d, args.framedump[1], args.framedump[2]]
        cmd += args.extra

        print("replay_run: %s" % " ".join('"%s"' % c if " " in c else c for c in cmd))
        try:
            p = subprocess.run(cmd, cwd=run_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               timeout=args.timeout)
        except subprocess.TimeoutExpired as e:
            out = e.stdout.decode("utf-8", "replace") if e.stdout else ""
            sys.stdout.write(out)
            print("replay_run: timed out after %g s" % args.timeout)
            return 4
        out = p.stdout.decode("utf-8", "replace")
        for line in out.splitlines():
            if line.startswith("replay:"):
                print(line)
        print("replay_run: exit status %d" % p.returncode)
        if window_failed(run_dir):
            # E.g. started from a session without access to the desktop.
            print("replay_run: the player could not open its window (allegro.log in %s)" % run_dir)
            if temp:
                args.keep = True
            return 5

        if args.hashlog:
            if os.path.exists(hash_in_run):
                shutil.copy2(hash_in_run, args.hashlog)
                print("replay_run: hash log -> %s" % args.hashlog)
            else:
                print("replay_run: the player wrote no hash log")
                return 5
        return p.returncode
    finally:
        if temp and not args.keep:
            shutil.rmtree(run_dir, ignore_errors=True)
        elif temp:
            print("replay_run: game folder kept: %s" % run_dir)


if __name__ == "__main__":
    sys.exit(main())

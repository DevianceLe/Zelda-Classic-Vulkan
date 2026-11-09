#!/usr/bin/env python3
"""Writes tests/replay/1st_quest_walk.zcr: a scripted input recording that
starts a new game of the 1st quest from an empty zc.sav (title screen,
register the name "link", start), then walks Link around: into the first
cave to take the sword, back out, across a few screens, swinging the sword.

    python -I tests/replay/make_1st_quest_walk.py [output.zcr]

The input is written the way the player records it (docs/zplayer-replay.md):
key[] changes and keyboard-buffer entries per frame. It assumes the
default key bindings (arrows, Alt = A, Ctrl = B, Enter = Start) and the
default name entry mode (typing on the keyboard), which the zc.cfg used by
tools/replay_run.py has.
"""

import os
import sys

# Allegro scancodes (allegro/keyboard.h)
KEY = {"A": 1, "I": 9, "K": 11, "L": 12, "N": 14,
       "ENTER": 67, "LEFT": 82, "RIGHT": 83, "UP": 84, "DOWN": 85,
       "LCONTROL": 117, "ALT": 119}
ASCII = {"I": ord("i"), "K": ord("k"), "L": ord("l"), "N": ord("n"), "ENTER": 13}
CHAR_KEYS = {"I", "K", "L", "N", "ENTER", "LEFT", "RIGHT", "UP", "DOWN"}

SEED = 20261107   # what init_game() passes to srand()

# The zc.cfg settings this script relies on (the defaults); playback warns
# if the player's zc.cfg differs.
CFG = [("key_up", KEY["UP"]), ("key_down", KEY["DOWN"]), ("key_left", KEY["LEFT"]),
       ("key_right", KEY["RIGHT"]), ("key_a", KEY["ALT"]), ("key_b", KEY["LCONTROL"]),
       ("key_s", KEY["ENTER"]), ("name_entry_mode", 0), ("title", 2)]


class Script:
    def __init__(self):
        self.f = 0
        self.lines = []     # (frame, order, text)
        self.n = 0

    def _add(self, frame, text):
        self.lines.append((frame, self.n, text))
        self.n += 1

    def wait(self, frames):
        self.f += frames

    def down(self, name):
        self._add(self.f, "key %s 1" % name)
        if name in CHAR_KEYS:
            # what readkey() returns: scancode << 8 | ASCII (0 for arrows)
            self._add(self.f, "char 0x%04x" % ((KEY[name] << 8) | ASCII.get(name, 0)))

    def up(self, name):
        self._add(self.f, "key %s 0" % name)

    def press(self, name, hold=4, after=0):
        self.down(name)
        self.wait(hold)
        self.up(name)
        self.wait(after)

    def walk(self, direction, frames, swing_every=0):
        """Hold an arrow key; optionally tap the A button (sword) now and then."""
        self.down(direction)
        done = 0
        while done < frames:
            step = frames - done
            if swing_every:
                step = min(step, swing_every)
            self.wait(step)
            done += step
            if swing_every and done < frames:
                self.press("ALT", hold=2)
                done += 2
        self.up(direction)

    def seed(self, frame, value):
        self._add(frame, "seed %d" % value)

    def text(self, end):
        out = ["zcreplay 1",
               "# Scripted: new game of the 1st quest, walk around (tests/replay/make_1st_quest_walk.py)"]
        out += ["cfg %s %d" % c for c in CFG]
        for frame, _, t in sorted(self.lines):
            out.append("%d %s" % (frame, t))
        out.append("end %d" % end)
        return "\n".join(out) + "\n"


def build():
    s = Script()

    # Title screen: Start.
    s.wait(60)
    s.press("ENTER", hold=5, after=60)

    # Game select: the cursor is on REGISTER (no saved games). Start.
    s.press("ENTER", hold=5, after=40)

    # Type the name and confirm with Enter (one key per few frames: the
    # name entry reads one buffered key per frame).
    for c in "LINK":
        s.press(c, hold=3, after=8)
    s.press("ENTER", hold=4, after=60)

    # Back on the game select screen, the cursor is on the new game. Start.
    seed_frame = s.f + 1
    s.press("ENTER", hold=5, after=0)
    # init_game() seeds rand() right after the selection; the frame is
    # checked on playback (a mismatch is reported as a desync).
    s.seed(seed_frame, SEED)
    s.wait(150)

    # Into the cave at the top left of the first screen for the sword.
    s.walk("UP", 40)
    s.walk("LEFT", 62)
    s.walk("UP", 70)
    # The old man's message types out (Link cannot move meanwhile).
    s.wait(305)
    # Step right under the sword, walk up to it and take it.
    s.walk("RIGHT", 5)
    s.wait(5)
    s.walk("UP", 45)
    s.wait(100)
    # Back out of the cave.
    s.walk("DOWN", 90)
    s.wait(40)
    s.walk("DOWN", 20)
    # Around the first screen and to the neighbours, swinging the sword.
    s.walk("RIGHT", 120, swing_every=30)
    s.walk("DOWN", 30)
    s.walk("RIGHT", 160, swing_every=40)
    s.walk("UP", 120, swing_every=30)
    s.walk("LEFT", 140)
    s.walk("UP", 100, swing_every=25)
    s.walk("LEFT", 90)
    s.walk("DOWN", 160, swing_every=35)
    s.walk("RIGHT", 60)
    s.press("LCONTROL", hold=3, after=20)
    s.walk("DOWN", 80, swing_every=20)
    s.walk("LEFT", 120)
    s.wait(60)

    end = max(s.f, 2400)
    return s.text(end)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "1st_quest_walk.zcr")
    with open(out, "w", newline="\n") as f:
        f.write(build())
    print("wrote %s" % out)


if __name__ == "__main__":
    main()

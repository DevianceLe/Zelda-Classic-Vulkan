# Replay samples

Recordings with the frame-hash log the Allegro build (`zelda` target) produces
for them. A player that renders the same frames produces the same log. See
`docs/zplayer-replay.md`.

| file | what |
|---|---|
| `1st_quest_walk.zcr` | New game of the 1st quest from an empty zc.sav: title screen, register "link", the sword cave, the first screens with octoroks and sword beams. 2478 frames. |
| `1st_quest_walk.framehash.txt` | Its hash log from the Allegro build (same with and without `-unthrottled`). |
| `make_1st_quest_walk.py` | Writes `1st_quest_walk.zcr`. |

Check a player against it (Windows, game data from a normal install, which
also has `alleg44.dll` for the Allegro build):

```
python -I tools/replay_run.py --exe <player.exe> --data "<data folder>" ^
       --replay tests/replay/1st_quest_walk.zcr --hashlog out.txt --unthrottled
python -I tools/replay_compare.py tests/replay/1st_quest_walk.framehash.txt out.txt
```

The expected log depends on the game data (zelda.dat, qst.dat, sfx.dat,
fonts.dat, 1st.qst of 2.53.0 Release 3) and the zc.cfg that `replay_run.py`
writes. If the game code changes what the 1st quest shows on purpose,
regenerate the log with the Allegro build (`--hashlog` of a playback) and
commit it with the change.

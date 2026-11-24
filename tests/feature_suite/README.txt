Feature test suite (2.55 / 3.0 play compared with upstream 3.0)
===============================================================

What it is
  quests\suite30.qst  upstream's z3 test quest (tests/replays/z3/z3.qst, saved as the newest 3.0 format by our
                      ZQuest) with one map + DMap per feature, saved by our ZQuest in its own version (3.0).
                      Its scripts are compiled by our ZQuest: z3's own (scripts\z3) and one global script that
                      runs the feature's script on the feature's DMap.
  features.txt        one line per feature: DMap/screen, scene edits, script, input (keys by frame), frames,
                      compared frames, what is checked and what to look at. The header explains the fields.
  expected\           upstream 3.0's result for each feature, recorded once (H:\zc-upstream\build-3.0, ef321f8,
                      headless replay): <name>.json = per compared frame a hash of the picture (6-bit colour)
                      with one small hash per row and column (to say where a difference is), Link's region
                      position by frame, the script lines; <name>.png = one cropped frame (for the sheet).
  scenes\             edits of each scene (from the r10/r11/r12 agents' scenes), applied to a copy of map 1.
  scripts\            feature scripts (the r10-r12 test scripts; each is wrapped in its own namespace), the
                      z3 quest's own scripts, te_rules.txt (Tranquil Expression's rules, set by the scripts
                      written for it before they run).
  coverage\           gaps.txt (the r12 list of upstream script commands/registers our engine lacked),
                      gen_coverage.py, cov_<group>.zs (every item called once: "C: NAME values" per item),
                      items.txt (how each item is called, or why it is skipped).
  build_quest.py      makes quests\suite30.qst from all of the above.

Run (needs only our build; upstream is not needed)
  python tools/feature_suite.py                    every feature, 6 at a time
  python tools/feature_suite.py features           all but the coverage groups;  "coverage" = only those
  python tools/feature_suite.py leever dive cov_*  some
  options: --jobs N, --build <folder with zplayer.exe> (default build/Release), --work <scratch folder>
           (default %TEMP%\zc_feature_suite), --keep (keep the compared frame dumps and logs)
  Our player runs from the build folder with a work folder per slot (hard links to the .dat files, a zc.cfg
  with fixed keys), windows shown and never focused (SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0), -replay of the
  feature's input, ZC_TEST_START at upstream's start, frame dumps only of the compared frames, and
  ZC_TEST_SAVE_RELOAD (test runs only: a script's Game->Reload() reads the save back from the save file, so
  save_reload / save_continue check what the save format keeps against upstream's replay save/reload; the
  switch set to frame numbers also saves and reloads at those frames, in any quest).
  Output: one line per feature
    SAME       pictures, Link's positions and script lines as upstream
    DIFF       and where: "hero N/M frames (first ...)", "lines N/M differ [the values that differ]",
               "frame F rows a-b x c-d" (upstream picture coordinates inside the crop), "same picture with
               rows=K" (only the camera height differs)
    DIFF~      a scene with random parts (enemy RNGs differ between the two players) differs
    MISSING    no expected data; FAILED our player did not run
  then the total, and for the coverage groups: how many items behave as upstream's, how many our player
  logs as unhandled ("ZASM: unhandled ...") and how many give other values; the list goes to
  <work>\coverage.txt. A run takes about half a minute; details (lines, sheets: ours | upstream | difference)
  in <work>\runs\<name>.

Record upstream again (only when a feature or the quest changes)
  python tools/feature_suite.py record [names] --upstream H:\zc-upstream\build-3.0
  Upstream runs headless (-headless -update test-mode replay, -snapshot), never with a window; each run in a
  folder of hard links to upstream's files (its own allegro.log). Only the small data above is kept.

Add a feature
  1. A scene: an edits file in scenes\ (lines "screen N key value", "enemy N key value", "combo N key value",
     "item N ...", "dmap 0 ..." as zqeditor -qst-scene-edit takes them; screens are map 1's: 136 + screen),
     and/or a script in scripts\ (a "global script X { void run() {...} }" printing "T<letter>: ..." lines;
     "// frames: N" for coverage scripts).
  2. A line in features.txt with a new DMap number (300-399 scenes and scripts, 400+ coverage).
  3. python -I tests/feature_suite/build_quest.py <z3new.qst> <build folder> [work folder]
     z3new.qst: upstream's tests/replays/z3/z3.qst saved by our ZQuest in the newest 3.0 format (zqeditor -qst-save-newer z3.qst z3new.qst 30 report). The build's zqeditor needs the test-only switch
     -qst-scene-edit (zq_scene_edit_tool.patch in this folder, applied to a test copy only: r10/r11/r12's tool + "item|enemy|dmap
     N copy M", "map N copy M", "map N enemyswap.E NE"); apply it to a copy, never to the program's source.
  4. Record upstream (above) for all features (the quest changed), then run.
  Coverage scripts: python -I coverage/gen_coverage.py <upstream 3.0 resources/include/bindings>
  src/port/zscript30/components/zasm/table.cpp (rewrites cov_*.zs and items.txt from gaps.txt).

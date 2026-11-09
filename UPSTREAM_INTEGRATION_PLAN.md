# One engine for ZC 2.53.444

> **Replaced on 2026-11-08.** The direction below, one engine built from upstream's
> code, is cancelled. Your port (`src/` and zcport) is the engine, and upstream's
> features get backported into it. `engine/` is now only a place to take code from.
> The first backport is 2.55 quests, described in the next section.

## Backport: 2.55 quests (in progress)

The player loads quests made by 2.55 through a separate loader in
`src/port/game/q255*.cpp`, so the readers 2.53 quests use don't change.

Who saves what: the player only plays newer quests and never converts them. The
quest editor is where a quest is saved between the older and newer formats, in
both directions. ZQuest refuses 2.55 quests for now, only until that exists,
because today's readers drop the data 2.53 has no field for.

`zplayer -q255-check in.qst report.txt` runs the section readers and reports each
one against the size the file declares. `zqeditor -qst-sections in.qst report.txt`
lists a quest's section versions.

Tried so far, each reading every section byte-exact:

| Quest | Build | State |
|---|---|---|
| Cambria The Lost Worlds | 61 | Plays through its story to the first playable screen |
| Shadow Knight, with its OST | 61 | Plays through its story into Sommelio Town: walking, collision, doors and OGG music work. Enemies, items and combat not tried |
| Alarm Clock | 52 | Runs with no script errors, but its text doesn't appear and Link stays visible |
| Tranquil Expression (3.0) | 61 | Plays through its intro, title and the first house cutscene. Walking not reached yet |
| Mystical Harps (3.0) | 61 | Attract sequence, title and Start work; walking, scrolling and swimming on the first screens. Combat not tried, and its music is silent |

3.0 quests use the same readers, now up to upstream's newest section versions. Their
scripts are one shared stream, run through stubs (`ZASM30_ENTER` in `src/ffscript.cpp`),
with a garbage collector for arrays and screen scripts added.

Still to do, largest first:
0. **Missing 3.0 systems.** Now in: music objects, OGG sounds, combo triggers and most new
   combo types, and every script type. Still to do: lifting, switch hook, new dark rooms
   and torches, slopes, z-heights, placed flags past 2.53's range, script file I/O, and
   the magenta strip under some 3.0 active subscreens. Combo behaviours beyond what the
   test quests reach have compiled but not been played.
1. **Quest rules.** 2.53 keeps 20 of the 100 rule bytes, and the rest change how
   2.55 quests behave.
2. **Game behaviour** behind the fields read and dropped. Each `q255_*.cpp` lists
   what it drops: enemy attributes and defenses, item fields and scripted
   families, combo types past 147, flags past 104, dmap settings, script types
   with no slot here.
3. **Tiles in ZQuest.** The player now holds 214,500 tiles, the most 2.55 allows,
   with 32-bit tile numbers (`tile_id` in `zdefs.h`). ZQuest stays at 65,520 tiles
   and 16-bit numbers, so opening a quest like Shadow Knight (67,296 tiles) there
   will need the same widening when format conversion is built.
4. **Versions in between.** The readers now take every version up to upstream's
   newest, but only the versions in the five test quests have been run.
5. **Saved games.** Not tried yet for a 2.55 quest.
6. **Saving between formats in ZQuest.** Open a 2.55 quest, and save a quest as
   either format. Needs a 2.55 writer, and the data read and dropped today has to
   be kept so nothing is lost saving back to 2.55 (keeping each unedited section's
   original bytes is the simplest way). Saving down to 2.53 loses whatever 2.53
   can't store, so it needs a warning.

Upstream: github.com/ZQuestClassic/ZQuestClassic, `main` at `ef321f8`
(2026-11-08, just after 3.0.0-prerelease.236). The working copy is in `engine/`.
A fuller reference copy is in `..\ZQuestClassic-upstream`.

## The goal

One engine that plays every quest, from the oldest formats through 2.53, 2.55
and 3.0, on our SDL3 + Vulkan + ImGui layer (zcport). It is built from
upstream's 3.0 `main`, which already reads all of those formats.

| Quest made in | Plays | Saves as |
|---|---|---|
| 2.53 and older | Yes, through upstream's compatibility rules | **2.53 format** (new work, Phase 6) |
| 2.55 | Yes | 3.0 format. Upstream has no 2.55 writer, so original 2.55 can't reopen it |
| 3.0 | Yes | 3.0 format |

Test quests supplied: `Tranquil Expression` (3.0) and `Cambria The Lost Worlds`
(2.55). The 3.0 quest's header reports program version `0x0255` build 61, the
same as 2.55, so the program version alone can't tell the two apart.

Today's 2.53 engine (`src/`) stays in the tree, unchanged, as the working product
and the reference to compare against. It is retired only when the new engine
passes the comparison in Phase 7, and that is your call.

## Why this is one engine and not a merge

The two repos share no git history. Upstream is 518,671 lines of C++20 against
our 234,986 lines of C++98. Adding upstream's features to our code one at a time
would mean redoing about eight years of work, so the new engine starts from
upstream's code and takes on our platform layer instead.

## How far apart the two are

| | Our fork | Upstream |
|---|---|---|
| Language | C++98 (game), C++17 (zcport) | C++20 |
| Platform | zcport: SDL3, Vulkan, ImGui, no Allegro | Own Allegro 5 fork plus an Allegro 4 layer (allegro_legacy) |
| Windows build | 32-bit | 64-bit by default; script JIT is x64 and ARM64 only |
| Combo format | 7 | 73 |
| Item format | 25 | 71 |
| Enemy format | 24 | 60 |
| Script format | 6 | 31 |
| Save format | 11 | 54 |
| Compatibility rules for old quests | n/a | At revision 122 |
| Replay tests | 1 walk-through | 641 data files, 730.6 MB |

## How much platform work there is

These counts come from matching names in the source, so treat them as close
estimates. Upstream compiles against the Allegro API, the same way 2.53 does,
which is why zcport can carry over.

| Area | Size | Status |
|---|---|---|
| Allegro 4 calls | 209 names used | 189 already in zcport, 20 missing (mostly upstream's Allegro 5 bridge) |
| Allegro 5: bitmaps and locks | 244 calls | New |
| Allegro 5: drawing | 191 calls | New |
| Allegro 5: audio | 87 calls, 43 functions | New, on our mixer |
| Allegro 5: display | 83 calls | New, mostly size and position queries |
| Allegro 5: everything else | 225 calls | New: files, config, input, threads, events, fonts, shaders |
| Dialog controls | 103 kinds | 25 done; 16 shared ones need host versions; 61 are app-specific and draw themselves |
| Dialogs | 88 builder classes, 64 raw tables | Builder widgets create the same `DIALOG` records our host already reads |

All 830 Allegro 5 calls (207 functions) get from-scratch replacements, in the
same style as zcport's Allegro 4 layer.

## Rules for every phase

1. The 2.53 engine keeps working: 1st, 2nd, 5th and Dream's Realm re-saves stay
   byte-identical to the references, and the `1st_quest_walk` replay stays
   identical across all 2478 frames.
2. Our `src/` game files don't change while the 2.53 engine is the product.
3. No Allegro-derived code and no jwin in what ships. Already left out of
   `engine/`: `third_party/allegro_legacy`, `third_party/al_gif`,
   `third_party/al_loadpng`, `src/al5_img`, `src/gui/jwin.cpp`,
   `src/gui/jwin_a5.*`, `src/core/jwinfsel.cpp`.
4. 2.53.444 remains a display-only number.

## Phases

### 0. Bring the code in (done, not committed)
Upstream's source and game data are in `engine/`, minus the files in rule 3.
Committing it to a vendor branch, so later upstream updates can be merged in,
is waiting for your OK.

### 1. Compile headless against zcport (done)
All 147 player sources compile 64-bit against zcport and link with no Allegro.
`zplayer -headless -dump-qrs` reads 1st.qst (2.53.0), Cambria (2.55.0 Alpha 107)
and Tranquil Expression (3.0.0-prerelease.205). Build files are in
`src/port/engine/`, output in `build-engine/`. The jwin controls are inert
placeholders until Phase 4 (`src/port/engine/a5/dialog_placeholders.cpp`).

### 2. Player on Vulkan with sound (in progress, large)
Working: the engine's layers are composited in software and shown through the
Vulkan window. All three quest eras draw a real game screen, with correct colors,
when started with `-test`. Sound (effects, OGG, MP3, WAV) and joysticks are written
but have not been heard or tried yet. On-screen text uses the default font for now.
Still to do: custom bitmap fonts, shaders, MP3-style streaming instead of decoding
whole files, and moving the compositing onto the GPU.
`ZC_ENGINE_OFFSCREEN="file.bmp,frames"` renders a frame with no window at all.
*Done when:* all three quest eras play from the title screen with sound.

### 3. Prove it plays correctly (medium)
Run upstream's replays, script JIT off and then on. Start with `classic_1st`,
`demosp253`, `nes_remastered` and `darkness_within`, 58.5 MB together.
*Done when:* they match upstream's recorded results.

### 4. Player menus in ImGui, jwin removed (in progress, large)
The engine uses your ImGui GUI from before upstream came in: the dialog host, your
menu bar (`src/port/engine/a5/system_menu.cpp`), your Video Settings and your file
browser. Nothing from jwin is compiled. Tried in a no-focus window on Display 3:
menu bar, About, Sound Settings (slider, drop-down, Cancel), Video Settings, More
Options, Control Schemes, the key-binding editor (all three tabs, scroll bar, "Press
any key" prompt), New File, the file browser, Quest Directory, Select Custom Quest,
and a 3.0 quest started normally through to its opening cutscene.
Written but not tried yet: the color swatch picker, in-dialog tooltips (only the
debugger uses them) and the script Directory list.
Still to do: the ZScript debugger. It opens a second window of its own, which
zcport doesn't provide yet, so Debug > Open debugger is greyed out and the
`debugger` config setting is ignored.
*Done when:* the player's dialogs work and the link output shows no jwin or
Allegro code.

### 5. Editor (large, Windows only)
`zeditor` on the same layers: 127,015 lines, 171 Allegro 5 calls, and many
self-drawn pages. Carry over the no-password prompt and Live Large/Small.
*Done when:* quests of each era open, edit and save correctly.

### 6. Save as 2.53 (large, the riskiest part)
Bring our 27 section writers (`zq_class.cpp`) over to the new engine's data.
Upstream's loader rewrites old data into its newer form as it reads, so writing
it back exactly is not automatic. Two safeguards:
- Keep each untouched section's original bytes and write those back, so only
  edited sections are rebuilt.
- Refuse or warn when a 2.53 quest uses something 2.53 can't store.

*Done when:* the four reference quests re-save byte-identical, and an edited
quest opens in the original 2.53.

### 7. Compare against the 2.53 engine, then retire it (medium)
Play the same inputs through both engines and compare frame by frame. Upstream's
compatibility rules aim to match old behaviour, but they don't guarantee it
frame for frame on real 2.53, so differences need reviewing one by one.
*Done when:* you're satisfied with the results and choose to retire `src/`.

### 8. Release, then Android (medium)
Package the one engine for Windows, extend the source zip and README. Android
follows as 64-bit, player only, with JIT off at first.

## What has to be carried over by hand

| Feature of ours | In the new engine |
|---|---|
| Vulkan, SDL3, ImGui dialogs, fullscreen modes | Comes with zcport |
| Video Settings | Reconnect to upstream's config |
| Android touch controls | Reconnect in Phase 8 |
| No quest password prompt | Re-apply; upstream still prompts (`zq_class.cpp:6297`) |
| Live Large/Small switching | Redo; upstream's editor has its own compact mode |
| Intro logo and credit line | Redo if wanted |
| Window titles, About text | Set to the fork's names |

## Saved games

On first run upstream copies `zc.sav` into its saves folder and then **deletes
the original** (`move_legacy_save_file` in `src/zc/saves.cpp`). It also uses the
same names as 2.53 for `zc.cfg`, `zquest.cfg` and `sfx.dat`. Until the 2.53
engine is retired, the new engine must run from its own folder, and that delete
gets changed to keep the original.

## Turned off in the fork

The updater, replay upload, Sentry crash reports and the launcher. These talk to
upstream's servers or would replace our build with theirs. Also skipped: the
ZScript debugger's websocket connection and the native file dialog library. Our
ImGui file browser takes that dialog's place.

## Risks

- **Upstream's format is still moving.** 3.0 is a prerelease, so a quest saved by
  a pinned build isn't guaranteed to stay valid.
- **2.53 accuracy.** One engine means 2.53 quests depend on upstream's
  compatibility rules instead of the real 2.53 code. Phase 7 measures this.
- **Save as 2.53** may not reach byte-identical for every quest. Phase 6 will show.
- **Graphics differences.** Blending and shaders may not match Allegro 5 exactly.
- **Threading.** Upstream's display assumptions may differ from zcport's platform
  thread.

## Licensing

Both projects are GPLv3, so they can be combined. Keep upstream's `AUTHORS` and
license files, and the source zip must cover the engine. If any of this is ever
sent back upstream, their `docs/ai.md` requires saying AI tools were used.

## Downloads still needing your approval

| What | Size | When |
|---|---|---|
| 2.53-era replay data | 58.5 MB | Phase 3 |
| Full replay data (optional) | 730.6 MB | Phase 3 or later |
| asmjit (script JIT) | To be measured | Phase 3 |

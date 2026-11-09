# zplayer: the player without Allegro

Goal: the player (`zelda` target) runs on SDL3 + Vulkan with no Allegro, first on
Windows, later on Android. ZQuest keeps using Allegro.

**Hard rule: existing quests must behave exactly as in the Allegro build.**

## Approach

- **Compatibility layer.** `src/port/` implements the ~263 Allegro functions and
  types the player uses, under the same names. The game code (about 126k lines,
  shared in part with ZQuest) compiles unchanged against it.
- **Software rendering stays.** The game keeps drawing 8-bit frames with a
  256-colour palette in memory. The layer converts each finished frame to BGRA
  and `VulkanPresenter` (`src/port/vk_present.*`) draws it scaled into the window.
- **Bit-exact parts reuse Allegro 4's algorithms** (Allegro is giftware): packfile
  LZSS and password encryption (quest and `.dat` files), fixed-point math, blenders
  and colour maps, so loading and gameplay produce identical results.
- **Platform: SDL3** for window, input, gamepads, audio and the Vulkan surface; volk
  loads Vulkan at runtime (no `vulkan-1.lib`). Only Vulkan 1.0 core and
  `VK_KHR_swapchain`, for Android coverage.
- **MIDI: the system synthesizer where there is one** (winmm's MIDI mapper on
  Windows, the device the reference build plays on), behind a small backend
  interface; Android (no system MIDI) will get a bundled SoundFont synth as a backend.
- **Reference build.** The Allegro `zelda` target stays buildable. Both builds
  have deterministic input playback plus per-frame hashes
  ([zplayer-replay.md](zplayer-replay.md)); the port must produce the same
  frames as `tests/replay/*.framehash.txt`.

## Layout

| Path | What |
|---|---|
| `src/port/vk_present.*` | Vulkan presentation (done) |
| `src/port/shaders/` | GLSL sources and generated SPIR-V headers (`glslang -V --target-env vulkan1.0 --vn <name>`) |
| `src/port/vk_present_test.cpp` | Stand-alone check of the presentation path |
| `src/port/entry/` | The `zplayer` process entry point (SDL3), see "Entry point" |
| `src/port/game/` | Replacements for game files that cannot be built against zcport (`zcmusic_port.cpp` with `zcmusic_decoders.cpp`, `single_instance_port.cpp`) |
| `src/port/tools/zplayer_link_gaps.py` | Groups the unresolved symbols of a `zplayer` link by owning module (`docs/zplayer-link-gaps.md`) |
| `src/port/tools/zcmusic_smoke.cpp` | `zcmusic_smoke` (built on request): plays music files through the zplayer zcmusic code on zcport's audio streams, writes what the streams get to WAV files and prints levels and pitch |
| `third_party/` | SDL3, Vulkan-Headers, volk, the music decoders (see `third_party/README.md`) |

## Phases

1. **Picture and controls (silent).** Core types, memory bitmaps and blitters,
   palettes, fonts and text, packfile/datafile/config, fixed math, timers,
   keyboard and mouse, dialog engine for the in-game menus, display through
   Vulkan. Done when the 1st quest, Revali and Dream's Realm play with frames
   matching the reference build.
   - [x] SDL3 + Vulkan presentation path (`vk_present_test`)
   - [ ] Compatibility layer headers and core types
   - [ ] Bitmaps, blitters, sprites, primitives, translucency
   - [ ] Fonts and text
   - [ ] Packfile, datafile, config
   - [ ] Timers, keyboard, mouse
   - [ ] Dialog engine (in-game menus)
   - [ ] `zplayer` target builds and runs (the target, entry point and game-side
     changes are in; it links once the modules are merged, see
     `docs/zplayer-link-gaps.md`)
   - [ ] Frame-hash comparison against the reference build
2. **Sound.** Samples and voices, OGG, MP3, tracker formats, NSF/SPC/GBS/VGM
   (Game_Music_Emu), MIDI. See "Sound" below.
   - [x] Samples, voices and audio streams audible (zcport mixer, SDL3 audio)
   - [x] MIDI through winmm on Windows (same messages as the reference build)
   - [ ] OGG, MP3, tracker formats, GME (`zcmusic_port.cpp` is still silent)
   - [ ] MIDI on Android (software synth backend)
3. **Gamepads, fullscreen and display selection, screenshots.**
4. **64-bit cleanup.** `long` used as a 32-bit type in the script engine, file I/O
   and save data; required for Android (arm64). Compatibility-critical.
5. **Android.** Gradle/NDK project from SDL3's template, data and quest storage,
   touch controls, app lifecycle.

## Building

Configure as usual (see the README); `ZC_BUILD_ZPLAYER` (default ON) adds
`src/port`. The port's directory removes the project-wide `/SUBSYSTEM:WINDOWS`
linker flag because it breaks SDL3's configure checks; port executables set
`WIN32_EXECUTABLE` themselves.

The `zplayer` target (only with `ZC_PORT_SDL`, the default) links the objects of
`zplayer_compile_check` (so the game is compiled once for both), the entry point in
`src/port/entry`, `zcport_sdl` and `zcport`, with the static C runtime and the same
icon resource as `zelda.exe`. It is written to `<build>/<config>/zplayer.exe`, next to
the Allegro `zelda.exe`:

```
cmake --build build-wt --config Release --target zplayer -- -m -v:minimal -nologo
```

Until all modules are merged the link fails, so `zplayer` is not part of the default
(ALL) build; turn on `ZPLAYER_IN_ALL` once it links.

### 64-bit Windows (x64)

The Allegro targets link the prebuilt 32-bit libraries in `libs/win32`, so a 64-bit
build configures zplayer alone: `ZC_ZPLAYER_ONLY` (default ON when
`CMAKE_SIZEOF_VOID_P` is 8, OFF otherwise) skips zcsound, romview, zelda, zquest
and the ZScript parser (only ZQuest compiles scripts), and makes `zplayer` part of
ALL. The conformance harness builds only its port programs there (the reference
side needs the 32-bit `alleg44.lib`); compare them with the reference programs of
a 32-bit build.

```
cmake -S . -B build-x64 -G "Visual Studio 17 2022" -A x64
cmake --build build-x64 --config Release --target zplayer -- -m -v:minimal -nologo
```

`long` stays 32 bits on Windows (LLP64), so quest and save files and ZScript
arithmetic are unchanged; the x64 port programs give the same output as the 32-bit
ones in every non-SDL conformance test.

## Running zplayer

Like `zelda.exe`, the player opens `zelda.dat`, `fonts.dat`, `sfx.dat`, `qst.dat`,
the built-in quests and `zc.cfg` / `zc.sav` relative to the **working directory**, and
looks for `zelda.nsf` (title, Triforce and ending music) next to the executable.

- Set the CMake cache variable `ZPLAYER_DATA_DIR` to a folder with the game data
  (for instance the reference build's output folder). After every build the files
  `zelda.dat fonts.dat sfx.dat qst.dat *.qst *.nsf` from it are copied next to
  `zplayer.exe`. It defaults to `<source>/build/Release` when that folder has
  `zelda.dat`. `zc.cfg` is not copied: the player writes its own on first start.
- Then start `zplayer.exe` from its own folder (`cd build-wt/Release`). The Visual
  Studio debugger does that already (`VS_DEBUGGER_WORKING_DIRECTORY`).
- Do not run it with the working directory in a folder you want to keep unchanged:
  the player writes `zc.cfg`, `zc.sav` and `allegro.log` there.
- `zplayer.exe` and `zelda.exe` use the same single-instance lock (`zc.lck`, a named
  mutex), because they share save files when they run in the same folder: run them
  one after the other, or pass `-multiple` (which shows a confirmation dialog).

## Entry point

The game's `main()` (`src/zelda.cpp`) stays as it is. For the `zplayer` and
`zplayer_compile_check` targets CMake defines `ZCPORT_MAGIC_MAIN`, and the replacement
headers (`allegro/internal/alconfig.h`) then compile it as `zcport_game_main()` with C
linkage, the same trick as Allegro's "magic main". `END_OF_MAIN()` stays empty.
`src/port/entry/zplayer_main.cpp` (no `<allegro.h>`) is the real entry point:

| system | entry | then |
|---|---|---|
| Windows | `WinMain` | `SDL_RunApp(__argc, __argv, ...)`: the C runtime's ANSI argv, the same bytes the Allegro build's `main()` gets (SDL's own parsing would give UTF-8) |
| Android | `SDL_main`, exported via `SDL_main.h` and called by `SDLActivity` | — |
| others | SDL3's `main()` from `SDL_main.h` | `SDL_RunApp()` |

Each path calls `zcport_sdl_register()` (implemented by the platform layer: it
registers its hook table with `zcport_set_platform()`) and then `zcport_game_main()`.
The contract is in `src/port/include/zcport/entry.h`. On Android the entry point will
also have to change to the app's storage directory before the game starts (phase 5).

## Sound

`src/port/al/platform_sound.cpp` keeps its own model of voices, audio streams and
the MIDI player (positions advancing in real time, `midi_pos`, loops, return
values), measured against the reference build. That model is the only source of
truth for what the game reads back; the outputs only follow it, so the game behaves
the same with or without a sound device. Two details measured from the reference:
`midi_seek()` starts again from the beginning for a target at or before `midi_pos`,
plays the event it reaches at the player's next tick and un-pauses the player (the
game pauses again itself); an audio stream hands out all its 2 x `bufcount`
buffers at once at the start, then half the ring each time the voice has moved
into the other half (what drives the music decoders' pace and `ZCMUSIC::position`).
The mixer plays its own copy of a stream's ring, updated at the mixer time each
buffer was handed out, so refilling a half the voice just left never cuts what is
still being heard.

- **Digital.** `install_sound()` opens the platform's audio device
  (`audio_open` in `zcport/platform.h`; SDL3: one audio stream on the default
  playback device, 44100 Hz, 16-bit stereo). Every change to a voice is
  published as a timestamped snapshot; the mixer plays the snapshots on the same
  clock, a fixed delay (a little more than two device blocks, ~25 ms) behind, so
  what is heard is exactly what `voice_get_position()` reported that much earlier
  and nothing is cut at the start or before the end. 8/16-bit unsigned samples
  (Allegro's format), mono/stereo, linear interpolation (mixer quality 2), volume,
  pan (centre = half volume per side, as Allegro), `set_volume()`,
  `set_volume_per_voice()`, loops, `PLAYMODE_BACKWARD`/`BIDIR`; the output is
  clipped.
- **MIDI.** The player's events become the messages the reference build's MIDI
  player sends the Windows MIDI mapper (recorded from its driver): the MIDI volume
  scales controller 7 (`v * midi_volume / 256`), resets on song start and loop,
  all-notes-off on stop/pause/seek/end, the channel state restored after a seek.
  They go to a backend (`zcport/midi_backend.h`) from a thread of their own, so a
  slow synthesizer never delays the game or the player: winmm (MIDI mapper) on
  Windows (`src/port/platform/midi_winmm.cpp`), none (silent) elsewhere yet. A
  software synth can render into the mixer through the backend's `render` hook.
- **Check.** `tests/conformance/test_platform_audio_sdl_portonly.cpp` plays
  generated samples, a stream and a short song and checks the mixed output (it
  also writes it to `audio_smoke.wav` in the run's tmp folder);
  `test_platform_sound_sdl.cpp` keeps checking the model against the reference.

## Game-side changes for zplayer

All under `ZC_PORT` (or in the zplayer build's source list); the Allegro `zelda` and
`zquest` builds are unchanged.

- `src/zcmusic.cpp` is replaced by `src/port/game/zcmusic_port.cpp`: the same API on
  the decoders under `third_party/` instead of the Allegro add-ons (stb_vorbis for
  OGG, minimp3 for MP3, DUMB 2 for IT/XM/S3M/MOD, Game_Music_Emu 0.6.5 for
  NSF/GBS/SPC/VGM/GYM; `zcmusic_decoders.cpp`), feeding zcport audio streams with
  the original's rates, buffer sizes and volumes. What the game can observe follows
  `src/zcmusic.cpp`: which files load (ZScript `PlayEnhancedMusic()` returns it, the
  game falls back to MIDI otherwise; the MP3 loader's skipped first block),
  `ZCMUSIC::position` counting (the Triforce cutscene waits for it), GME track
  counts, the play/pause/stop states. OGG and MP3 files loop without the gap the
  original's stream restart makes; see the file's header comment.
  `zcmusic_smoke` (`src/port/tools`) checks the path without the game.
- `src/zelda.cpp`: no `zc_win_proc_fix` window procedure under `ZC_PORT`. The window
  is SDL's, a Unicode window, and `src/win32.cpp` calls the procedure pointer that
  `GetWindowLongPtrA` returns directly, which is invalid for such a window; SDL keeps
  the screen saver off itself. The setting is kept in `zc.cfg`.
- `is_only_instance()`: `src/win32.cpp` on Windows (unchanged), the Allegro build's
  `src/single_instance_unix.cpp` on desktop Unix, `src/port/game/single_instance_port.cpp`
  on Android (one activity instance at most).
- GIF screenshots: `zelda` takes `load_gif()`/`save_gif()` from the `al_gif` add-on
  library; `zplayer` compiles the game's own `src/load_gif.cpp`/`src/save_gif.cpp`
  (what ZQuest uses). `save_gif.cpp` assumes a 4-byte `long` (phase 4).
- The file selector: `src/jwinfsel.cpp` is replaced by a Dear ImGui file browser
  (`src/port/game/ui_filesel.cpp` with the window in `ui_filesel_view.cpp` and the
  OS listing in `ui_fs_os.cpp`): `jwin_file_select_ex`, `jwin_dfile_select_ex` and
  `jwin_file_browse_ex` keep their arguments, return values and what they leave in
  the caller's buffer (canonical path, the original's extension rules; see the
  file's header comment). It runs as a UI session: the game thread waits with
  `zcui::wait_frame()` and does all file system access, the window only queues
  requests. Folders first, filters, drives and places, breadcrumbs, keyboard and
  gamepad navigation.
- jwin drawing outside dialogs is ImGui drawing (`src/port/game/ui_msgbox.cpp`):
  the progress box of `box_start()`..`box_end()` in `src/zsys.cpp` (the player
  never shows it: it loads quests with `show_progress` off, but the `box_out()`
  log lines are written as before) and the "Press a key" boxes of the key and
  button settings in `src/zc_sys.cpp`. Both are drawn on ImGui's foreground draw
  list and take no input; the game's own wait loops are unchanged.
- Unchanged and working with the SDL window through `win_get_window()`: the
  `window_display` placement (`src/zsys.cpp`), the `-console` debug console, the
  ZScript debug console, `DwmFlush`, `timeBeginPeriod` (all Windows only).

## GUI: the dialog manager (`src/port/al/gui_*.cpp`)

Written from scratch; the behaviour was measured on the reference build with
scripted input and logging dialog procedures (`tests/conformance/test_gui_*`,
helpers in `tests/conformance/gui_conf.h`). The files are split by what they
link against, so the dialog manager links without the text and blit modules:

| File | What | Needs |
|---|---|---|
| `gui_engine.cpp` | init/update/shutdown_dialog, do_dialog, object/dialog/broadcast_dialog_message, offer_focus, utilities, globals, gui_mouse_* defaults | foundation, prim |
| `gui_focus_order.cpp` | arrow-key focus order (box gaps, the reference's quicksort tie order) | - |
| `gui_joystick.cpp` | joy[0] to arrow/space keys | joystick module, weakly (`/alternatename`, `#pragma weak`) |
| `gui_procs_basic.cpp` | d_yield/d_clear/d_box/d_keyboard_proc | prim |
| `gui_procs_text.cpp` | gui_textout_ex, gui_strlen, d_text/ctext/rtext/button/check/radio/shadow_box_proc | text, makecol |
| `gui_procs_blit.cpp`, `gui_popup.cpp` | d_bitmap_proc, d_icon_proc, popup_dialog | blit |
| `gui_procs_edit.cpp` | d_edit/list/text_list/textbox/slider_proc, gui_*_proc hooks | text |

Points worth knowing (all checked against the reference):

- `update_dialog()` returns 1 normally and TRUE (-1) while it waits for a
  double click; a click starts that wait (about 240 ms, 8 pixels) only if
  nothing else is pending in `player->res`. The reference samples the button
  for the double click in a timer thread; here it is sampled per update.
- With no object under the mouse, a button change, a held button and a wheel
  turn each idle the dialog once more, and the button change is not taken
  note of (`player->mouse_b` keeps its value), as in the reference.
- The arrow keys ignore the direction (left = right, up = down): candidates
  are ordered by the gap along the axis plus 8 times the gap across it, sorted
  like the reference's C library quicksort so that ties go the same way.
- `init_dialog()` clips the GUI bitmap to `SCREEN_W` x `SCREEN_H` (in the
  conformance tests, without a graphics mode, nothing drawn inside a running
  dialog shows unless the test resets the clip).
- Not implemented (the player does not use them): Allegro's menus (do_menu,
  init/update/shutdown_menu, d_menu_proc), alert, alert3, file_select_ex,
  gfx_mode_select*. d_list/text_list/textbox/slider_proc are approximations
  (the player only compares d_list_proc's address and replaces it before the
  dialog runs); d_edit_proc's drawing matched the reference in the cases
  compared, its key handling was not compared.

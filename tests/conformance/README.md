# Conformance harness: reference Allegro vs port

The SDL3/Vulkan player must behave **exactly** like the Allegro 4.4.2 build, or
existing quests break: same pixels, same fixed-point numbers, same file decoding,
same text metrics, same GUI behaviour. This harness is how a port module proves
that. Every test is compiled twice from the same source:

| program            | headers               | library                          |
|--------------------|-----------------------|----------------------------------|
| `conf_<name>_ref`  | `allegro/include`     | `libs/win32/alleg44.lib` (+ dll) |
| `conf_<name>_port` | `src/port/include`    | `zcport` (+ `zcport_sdl` for `*_sdl*`) |

Both print one line per check, `test_name/case: value`, and
`run_conformance.py` compares the two outputs line by line. **The reference
build is the authority**: when it and the port disagree, the port is wrong (or the
test reads something undefined; see `--check-determinism`).

## Files

| file | what |
|------|------|
| `conf.h` | the API tests use: `CONF_TEST`, `conf::Ctx` checks, `conf::Seq`, hashing, RNG |
| `conf_zc.h` | Zelda Classic data helpers: datafile password, `.qst` decoding (optional include) |
| `gui_conf.h` | GUI test helpers: scripted mouse/keyboard, logging dialog procedure, step logs (optional include; makes the reference use `SYSTEM_AUTODETECT`) |
| `conf.cpp`, `main.cpp`, `conf_crash.cpp`, `conf_internal.h` | framework implementation, entry point, crash handling |
| `CMakeLists.txt` | one ref and one port program per `test_*.cpp` |
| `run_conformance.py` | runs the pairs, diffs, PASS/FAIL table, failure details |
| `test_harness_smoke.cpp` | the pipeline smoke test (bitmap, putpixel, itofix/fixtoi) |

## Building

**Inside the game build** (how it is meant to be used). `src/port/CMakeLists.txt`
includes this directory *after* it defines `zcport` (and `zcport_sdl`), e.g.

```cmake
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/conformance/CMakeLists.txt)
    add_subdirectory(${CMAKE_SOURCE_DIR}/tests/conformance ${CMAKE_BINARY_DIR}/tests/conformance)
endif()
```

Port programs exist only if `zcport` is a target at that point (`_sdl` ones only
if `zcport_sdl` is too); otherwise configure prints a note and builds the ref
side alone. Then, in your own build directory:

```
cmake -S "<repo>" -B "<repo>/build-wf-<you>" -G "Visual Studio 17 2022" -A Win32 ^
      -DZC_PORT_SDL=OFF -DBISON_EXECUTABLE=... -DFLEX_EXECUTABLE=...
cmake --build "<repo>/build-wf-<you>" --config Release --target conf_all -- -m -v:minimal -nologo
```

Targets: `conf_<name>_ref`, `conf_<name>_port`, `conf_all_ref`, `conf_all_port`,
`conf_all`. Everything lands in `<build>/conformance/<config>/` next to a copy of
`alleg44.dll`. (`-DZC_PORT_SDL=OFF` skips SDL3 when you only need tests.)

**Stand-alone** (fast: no game, no SDL3, no bison/flex):

```
cmake -S "<repo>/tests/conformance" -B "<repo>/build-wf-<you>" -G "Visual Studio 17 2022" -A Win32
```

Here `zcport` is built locally from `src/port/al/*.cpp` with
`src/port/include` (the zcport convention; `CONF_BUILD_ZCPORT=OFF` disables it,
`CONF_PORT_DIR` points it at another port tree). No `zcport_sdl`, so `_sdl`
tests are ref-only. The authoritative result is the one from the game build.

Compiler settings match the game: `ALLEGRO_MSVC`, `ALLEGRO_HAVE_STDINT_H`,
`ALLEGRO_NO_COMPATIBILITY`, `DEBUGMODE` (as `src/zc_alleg.h`), static CRT,
`/fp:fast` in Release, 32-bit, console subsystem. The port programs never see
`allegro/include` (the directory's inherited include paths are cleared).

## Running

```
python -I tests/conformance/run_conformance.py "<build dir>" [options]
```

| option | |
|--------|--|
| `--config Debug` | configuration (default Release) |
| `-t harness_smoke` | one program; `-t harness_smoke/smoke_bitmap` one test in it; `-t smoke_bitmap` that test name in any program; globs work; repeatable |
| `--filter SUBSTR` | only tests whose name contains SUBSTR |
| `--data DIR` | game data (default `<repo>/build/Release`) |
| `--quests DIR` | custom quests (default `<repo>/../quests`) |
| `--check-determinism` | also run both sides with a disturbed heap (`--jitter`); fails tests whose output changes. **Run it on every new test.** |
| `--isolate` | every test in its own process (automatic after a crash or timeout) |
| `--ref-only` | run just the reference and print its lines (while writing a test, before the port has the functions) |
| `--allow-missing-port` | a program without a port build shows NO-PORT instead of FAIL |
| `--timeout S`, `--max-diffs N`, `--no-details`, `--work DIR`, `--list`, `-v` | |
| `-- ARGS` | extra arguments for the programs |

It builds nothing. Results: `PASS`, `FAIL`, `SKIP` (both sides skipped the same
way), `REF-OK` (ref-only mode), `NO-PORT`. Exit status 0 = no FAIL, 1 = some FAIL,
2 = nothing found / setup problem.

For each failing test it re-runs that test alone on both sides with `--expand`
(every `Seq` item printed) and `--dump` (raw bitmap / memory dumps), then shows:

- the first differing lines, including the first differing sequence item with
  its label (`smoke_fixed/fixtoi[4]: -1  ; fixtoi(0xfffe8000)` vs `0`);
- for bitmaps: how many pixels differ, their bounding box and the first few
  `(x,y) ref=.. port=..`; for memory/files: first differing offset with a hex view;
- crashes (`0xC0000005 (access violation)`, `abort()`, timeouts) attributed to
  the test that was running;
- copy-paste repro commands for both programs.

Outputs (`ref.txt`, `port.txt`, stdout/stderr, dumps) stay in
`<build>/conformance/run/<program>/`.

The programs themselves accept `--data --quests --filter --test --out --tmp
--dump --expand --jitter --system none|auto --quiet --list`; see `main.cpp`.

## Writing a test

Create `tests/conformance/test_<key>_<what>.cpp` (`<key>` = your module key),
re-run cmake (the glob uses `CONFIGURE_DEPENDS`, so building `conf_all` is
enough), build, run.

```cpp
#include "conf.h"

CONF_TEST(blit_masked_8bit)                 /* name: a C identifier, unique in the file */
{
    BITMAP *src = create_bitmap_ex(8, 32, 32);
    BITMAP *dst = create_bitmap_ex(8, 40, 40);
    for(int y = 0; y < 32; ++y)
        for(int x = 0; x < 32; ++x)
            putpixel(src, x, y, ctx.rng.range(0, 3));   /* 0 = transparent */
    clear_to_color(dst, 7);
    masked_blit(src, dst, 0, 0, 5, 3, 32, 32);
    ctx.check_bitmap("result", dst);

    conf::Seq s(ctx, "edges");                       /* many values, one line */
    for(int x = -40; x < 80; x += 7)
    {
        clear_to_color(dst, 7);
        masked_blit(src, dst, 0, 0, x, x / 2, 32, 32);
        s.add_bitmap(dst, "x=%d", x);                 /* label shown on failure */
    }
    s.end();
    destroy_bitmap(src);
    destroy_bitmap(dst);
}
```

The test function gets `conf::Ctx &ctx`. Checks (each prints `test/case: value`):

| call | prints |
|------|--------|
| `check_int(c, v)` / `check_hex(c, v)` / `check_bool(c, b)` | `-5` / `0x0001a000` / `true` |
| `check_double(c, d)` | `%.17g` plus the exact bits |
| `check_str(c, s)` | quoted, escaped; `(null)`; long strings get length + hash |
| `check_mem(c, p, n)`, `check_file(c, path)` | size + hash (dumped on failure) |
| `check_bitmap(c, bmp)` | `bmp WxHxD h:...`: hash of w, h, depth and every row via `bmp->line[y]` (dumped) |
| `check_clip(c, bmp)` | `clip=.. cl=.. ct=.. cr=.. cb=..` |
| `check_palette(c, pal, n)` | hash of r, g, b (not the filler) |
| `check_hash(c, h)`, `checkf(c, fmt, ...)` | anything else |
| `expect(c, cond, fmt, ...)`, `fail(c, fmt, ...)` | `ok` / `FAIL ...` (any FAIL value fails the test, even on both sides) |
| `skip(fmt, ...)` | `skipped: SKIP ...` |
| `note(fmt, ...)` | `# ...`: never compared, for humans |

`conf::Seq` (`add_int/add_hex/add_double/add_str/add_mem/add_bitmap`, `end()`) folds
thousands of values into one line and expands them on failure: use it for loops
over inputs (fixed-point tables, every glyph of a font, every object of a
datafile). Helpers: `conf::nm("glyph_%d", i)` for case names, `conf::Hash`,
`conf::hash_bitmap/hash_palette/hash_bytes`, `ctx.rng` (xorshift64*, seeded from
the test name: identical on both sides and independent of `--filter`),
`ctx.data(file)`, `ctx.quest(file)`, `ctx.tmp(file)` (per-run scratch directory:
write files only there), `ctx.have_data(file)`, `ctx.have_quest(file)`.

### Rules

1. **Only the public Allegro API the player uses** (see
   `src/port/notes/allegro_api_inventory.md`, plus what its macros expand to), so
   the same file compiles against both header sets. Include
   `<allegro/internal/aintern.h>` yourself if you need what the game takes from it
   (e.g. `FONT->vtable->char_length`). Read struct fields only where the game does
   (`BITMAP->line[]`, `->clip`/`cl`..`cb`, `DATAFILE` `.type/.dat/.size`, `MIDI`,
   `SAMPLE`, `joy[]`, `PACKFILE` flags, ...).
2. **Print values, never pointers, addresses, timings or absolute paths.**
3. **Never read memory the API does not define.** Hash `dat[i].size` bytes, not a
   string that happens to be there: the signature object of `fonts.dat` and
   `zelda.dat` is *not* NUL-terminated (the game uses `strncmp(..., 24)`), and
   `check_str` on it prints heap garbage. `--check-determinism` catches this kind
   of mistake; a failure there is a test bug, not a port bug.
4. **Deterministic inputs only**: `ctx.rng`, constants, game files.
5. **Leave global state as you found it.** Before every test the framework sets
   `set_color_depth(8)`, `set_color_conversion(COLORCONV_NONE)` and
   `packfile_password(NULL)` (the game's settings). Anything else you change
   (`font`, `set_uformat`, palette, `rgb_map`, `color_map`, drawing mode, config
   file, installed timers, ...) restore at the end, and free what you create.
6. Cover what quests can reach: edge cases, clipping, negative coordinates,
   sub-bitmaps, every colour depth the game supports, real data from
   `fonts.dat` / `zelda.dat` / quests, not only happy paths.
7. Case names: no `": "`, keep them stable.
8. Do not test other modules' functions (they have their own tests).

Workflow: write the test, run it with `--ref-only --check-determinism` until the
reference output looks right and is stable, then make the port match (full run,
again with `--check-determinism`). Never change a test to make the port pass
unless the reference output stays identical.

### Special file names

- `test_<x>_portonly.cpp`: no reference program (behaviour Allegro does not
  have, e.g. platform hooks). It passes when it exits normally and no value
  starts with `FAIL`, so use `expect`/`fail`.
- `test_<x>_sdl*.cpp`: the port program also links `zcport_sdl`; both sides call
  `install_allegro(SYSTEM_AUTODETECT, ...)` instead of `SYSTEM_NONE`. If the
  platform must be registered explicitly, do it in a hook (only on the port side):

  ```cpp
  #ifdef CONF_SIDE_PORT
  CONF_PRE_INIT(register_platform) { /* call the zcport_sdl init/registration API */ }
  #endif
  ```

  `CONF_POST_INIT(name)` runs after `install_allegro` and `set_color_depth(8)`.
  Prefer `SYSTEM_NONE` tests wherever the behaviour does not need a platform.

`CONF_SIDE_REF` / `CONF_SIDE_PORT` are defined on the respective side
(`conf::side()` gives `"ref"` / `"port"`): for hooks and notes, never to change
what a check prints.

## Game data, passwords and quests

`--data` defaults to `<repo>/build/Release` (read-only: never write there; copy
to `ctx.tmp()` first if a test must modify a file). What the player does, and
what a test must do to see the same bytes:

| file | password | notes |
|------|----------|-------|
| `zelda.dat` | none (`packfile_password(NULL)`) | object 0 = signature (not NUL-terminated); indices in `src/zeldadat.h` (`ZC_MIDI` = nested MIDI datafile) |
| `sfx.dat` | none | `SAMP` objects |
| `fonts.dat` | `conf::zc::datapwd()` | FONT objects (indices `FONT_GUI_PROP`, `FONT_NES`, ... in `src/fontsdat.h`) |
| `qst.dat` | `conf::zc::datapwd()` | the built-in quests, each 007-encoded inside the datafile |
| `*.qst` | wrapper, then `datapwd()` | see below |

The password is `datapwd` from `src/zsys.cpp` after `resolve_password()`;
`conf_zc.h` derives it the same way. Set it with `packfile_password()` right
before `load_datafile()` / `pack_fopen()` and reset it with
`packfile_password(NULL)` afterwards.

A quest file (`1st.qst`..`5th.qst`, `quests/q839.../The Legend of Revali.qst`,
`quests/q804.../Dreams_Realm 2.53.qst`, ...) is wrapped by `encode_file_007`
(header `"Zelda Classic Quest File"`, key, scrambled bytes, checksums). Inside is
an LZSS-packed, password-scrambled packfile. `conf_zc.h` reproduces the game's
`open_quest_file()` path with the same Allegro calls:

```cpp
#include "conf.h"
#include "conf_zc.h"

CONF_TEST(file_quest_revali)
{
    const char *q = "q839_TheLegendofRevaliHatesEnd/The Legend of Revali.qst";
    if(!ctx.have_quest(q)) { ctx.skip("quest not found"); return; }
    std::string dec = ctx.tmp("revali.dec");
    int method = -1;
    ctx.check_int("decode", conf::zc::decode_quest(ctx.quest(q).c_str(), dec.c_str(), &method));
    ctx.check_int("method", method);              /* 4 = ENC_METHOD_211B18 */
    PACKFILE *f = conf::zc::open_decoded_quest(dec.c_str());   /* datapwd, F_READ_PACKED */
    ctx.expect("open", f != NULL);
    if(!f) return;
    conf::Hash h; long n = 0; int c;
    while((c = pack_getc(f)) != EOF) { h.add_u8(c); ++n; }
    pack_fclose(f);
    ctx.checkf("unpacked", "%ld bytes %s", n, conf::hash_text(h.value()).c_str());
}
```

`decode_quest()` handles `"qst.dat#OBJECT"` names too (read through the
packfile layer with `datapwd()`, as the game does). On the reference this
decodes and unpacks 1st/2nd/5th.qst, Dreams_Realm 2.53 and The Legend of Revali
with matching checksums. Big quests unpack to ~100 MB: hash while streaming, do
not print per-byte values, and expect a few seconds per run.

Tests that need data should `skip()` when it is missing, so the result shows
SKIP instead of a false PASS or a confusing FAIL.

## What the port must provide for the framework itself

`main.cpp`/`conf.cpp` use only: `install_allegro`, `SYSTEM_NONE`,
`SYSTEM_AUTODETECT`, `allegro_error`, `allegro_exit`, `set_color_depth`,
`set_color_conversion`, `COLORCONV_NONE`, `packfile_password`,
`create_bitmap_ex`, `clear_to_color`, `destroy_bitmap`, `bitmap_color_depth`,
`BITMAP::w/h/line[]`, `RGB::r/g/b`, `PAL_SIZE` (and, on the reference side only,
`register_assert_handler`). `ALLEGRO_USE_CONSOLE`, `DEBUGMODE` and
`ALLEGRO_NO_COMPATIBILITY` are defined before `<allegro.h>`; `main` is `#undef`ed
after it, so the headers may define a magic main if they want.

## Exit codes of the programs

`0` finished (whatever the results), `2` bad arguments, `3` `abort()`, `4`
`install_allegro` failed, `5` Allegro `ASSERT` on the reference side, larger
values are Windows exception codes. Crashes print `# CRASH in test '<name>'`
and never open an error dialog, so unattended runs cannot hang.

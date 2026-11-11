# ZC Vulkan 2.53.444.4

[![Build](https://github.com/DevianceLe/Zelda-Classic-Vulkan/actions/workflows/build.yml/badge.svg)](https://github.com/DevianceLe/Zelda-Classic-Vulkan/actions/workflows/build.yml)

> [!WARNING]
> **Highly experimental.** This fork is a work in progress. Expect bugs and crashes, and back up your quests and save files before you use it. A quest saved as 2.55.17 or 3.0 Nightly can't be opened by older versions of Zelda Classic.

Zelda Classic is a game engine and quest editor for making and playing custom adventures in the style of the original *The Legend of Zelda*. This repository is a community fork of Zelda Classic 2.53, rebuilt to run on SDL3 and Vulkan.

| Program | What it is | Runs on |
|---|---|---|
| `zplayer` | The game. Plays 2.53, 2.55 and 3.0 Nightly quests. | Windows 10/11, Linux (64-bit), Nintendo Switch (homebrew), Android 8.0+ (in progress) |
| `zqeditor` | ZQuest, the quest editor. Edits quests up to 2.53.1. | Windows 10/11 |

## Downloads

Get the latest Windows zip, Linux `.tar.gz`, Switch zip and Android APK from the [Releases page](https://github.com/DevianceLe/Zelda-Classic-Vulkan/releases/latest). Earlier builds are in [`downloads/`](downloads/).

Want to know if a quest works? See the **[quest compatibility list](COMPATIBILITY.md)**.

## Mission statement

> **Keep Zelda Classic alive on modern machines without breaking a single quest.**
>
> The quests this community has made over more than twenty-five years are the reason this engine matters, so they come first. We are rebuilding the classic 2.53 engine on modern technology, with no Allegro left in it, so it runs well on today's PCs and on Android. On that foundation we are bringing in the newer work from the ZQuestClassic project, so one engine can play every generation of quest.

What that means in practice:

1. **2.53 quests never change.** A 2.53 quest plays exactly as it always has. A 2.53 quest saved here still opens in the original Zelda Classic 2.53.
2. **Modern foundations.** SDL3 for windows, input and audio, Vulkan for graphics, and our own compatibility layer instead of Allegro.
3. **Everywhere people play.** Windows, Linux and Nintendo Switch homebrew today, Android next.
4. **Every generation of quest.** The player runs 2.55 and 3.0 Nightly quests alongside 2.53 ones, and it never saves a newer quest as an older one.
5. **An editor for all of it.** ZQuest will become the place where quests convert between the older and newer formats, in both directions.

## Project status

**Working**

- Vulkan rendering with window sizes, resolutions, scaling and fullscreen modes
- New Dear ImGui menus, dialogs and file browser in the player and the editor
- ZQuest on SDL3 and Vulkan, with Large and Small layouts you can switch while it runs
- 2.55 and 3.0 Nightly quest files, scripts of every type, music objects, and combo triggers in the player
- On 2.55 and 3.0 Nightly quests: slopes, bottles and bottle shops, the switch hook, sprite rotation and scaling, script lighting and dark rooms, subscreen widgets and overlay subscreens. Each has been tried in one to four quests so far
- Older quests (1.90 to 2.50.1) keep more of how their own version played, from a survey of the original source branches
- The player on 64-bit Linux, tested on Debian 13 and Arch Linux with Mesa's software Vulkan driver
- The player as Nintendo Switch homebrew, tested in the Eden emulator only

**Written, but not yet confirmed in play on 2.55 and 3.0 Nightly quests**

- Lifting and throwing, torches and lantern
- Ice, push blocks, new mirrors, screen tints, light beams, bridges and pitfalls
- Shadows under flying weapons and items
- Script file, directory and stack commands

**Still to do**

- Z-heights
- Subscreens with more than one page (only the first page shows)
- The Moon Pearl and bunny state, the magic mirror, and the bug net
- The 2.55/3.0 Nightly editor. Until it exists, ZQuest refuses quests newer than 2.53.1 and closes. The player still plays them.
- ZQuest on Linux

The [compatibility page](COMPATIBILITY.md#what-still-doesnt-work) has the full list of what still doesn't work, including known bugs.

## Getting started

Download the latest zip from the [Releases page](https://github.com/DevianceLe/Zelda-Classic-Vulkan/releases/latest), unzip it somewhere you can write to (not Program Files), and run `zplayer.exe` or `zqeditor.exe`. You need a graphics driver with Vulkan support.

To play a quest directly:

```bash
zplayer -windowed -standalone "My Quest.qst"
```

Put a quest's music next to the `.qst` file or in a `Music` folder beside it.

On Linux, unpack the `.tar.gz` and run `./zplayer` from inside the folder. It's one build for nearly every current distribution: you need glibc 2.30 or newer, the Vulkan loader, and a Vulkan driver. ZQuest is Windows only for now.

On Switch, copy the `switch` folder from the zip to the root of your SD card and start `zplayer` from the Homebrew Menu. Put your quests in `sdmc:/switch/zplayer/`.

| Key | Button |
|---|---|
| Arrows | Move |
| Z | A |
| X | B |
| Enter | Start |
| Space | Map |

## Building from source

You need Visual Studio 2022 with the x86 C++ tools, and CMake. Flex and Bison for Windows are included. Run these from PowerShell in the repository root:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DBISON_EXECUTABLE="$PWD/other/winflex/win_bison.exe" -DFLEX_EXECUTABLE="$PWD/other/winflex/win_flex.exe"
```

```bash
cmake --build build --config Release --target zplayer zqeditor
```

The programs are written to `build/Release`. They need the game data beside them (`zelda.dat`, `zquest.dat`, `sfx.dat`, `fonts.dat`, `qst.dat` and the default quests), which you can copy from `packaging/windows/`.

The Android player is a Gradle project in [`android/`](android/). It targets `arm64-v8a` and `x86_64`.

To build the player on Linux, install the tools and libraries (these are the Debian 13 names):

```bash
sudo apt install build-essential cmake ninja-build pkg-config flex bison libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev libxfixes-dev libxkbcommon-dev libwayland-dev wayland-protocols libdrm-dev libgbm-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libdbus-1-dev libudev-dev libvulkan-dev
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DZC_ZPLAYER_ONLY=ON -DZC_PORT_SDL=ON
```

```bash
cmake --build build --target zplayer
```

The program is written to `build/zplayer`. It needs the same game data beside it, plus a General MIDI `soundfont.sf2` for MIDI music.

The Switch build uses devkitPro's `devkitpro/devkita64` container:

```bash
cmake -S . -B build-switch -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake -DCMAKE_BUILD_TYPE=Release -DZC_ZPLAYER_ONLY=ON -DZC_BUILD_ZPLAYER=ON
```

```bash
cmake --build build-switch --target zplayer_nro
```

## Quest compatibility

**[Quest compatibility list](COMPATIBILITY.md)**: the result for every quest in the PureZC collection, more than 800 quests.

| Quest made in | Player | ZQuest |
|---|---|---|
| 2.53.1 and earlier | Plays exactly as before | Opens and saves as 2.53 |
| 2.55 and 3.0 Nightlies | Plays; never saved as an older quest | Refused for now |

Every change has to pass the 2.53 checks. Re-saved reference quests must match byte for byte, and [replays](tests/replay/) must produce identical frames.

## Repository layout

| Path | What is in it |
|---|---|
| `src/` | The game and editor code |
| `src/port/` | The SDL3 and Vulkan port, including our Allegro replacement |
| `src/port/game/` | Readers and engine code for 2.55 and 3.0 Nightly quests |
| `android/` | Android project |
| `third_party/` | Bundled libraries ([details](third_party/README.md)) |
| `tests/replay/` | Recorded play-throughs and their expected frame hashes |
| `packaging/` | Game data and notes that ship with each release |
| `.github/workflows/` | Automatic builds and releases |
| `downloads/` | Earlier builds |
| `docs/` | Notes on the port |

The original Allegro `zelda` and `zquest` targets stay in the tree only as a reference build to measure the port against.

## Releasing

Every push to `main` builds Windows, Android, Linux and Nintendo Switch. To publish a new release:

1. Change the number in `VERSION`.
2. Update `packaging/RELEASE_NOTES.md`.
3. Commit and push.

GitHub builds it and publishes a release for that version. If a release with that number already exists, nothing is published. A version ending in `-PRE` (for example `2.53.444.4-PRE`) is built on every push but never published, so work in progress can be pushed safely.

## Credits

Zelda Classic has been a community effort since 1999. Thank you to everyone below. Some people appear under more than one name, and if we have missed you or got something wrong, please tell us.

### This port

- **Deviance**: SDL3 and Vulkan port, Android, Linux and Nintendo Switch ports, and 2.55 and 3.0 Nightly quest support

### Creators

- **Phantom Menace** (Jeremy Craner): creator of Zelda Classic
- **War Lord**: co-creator of the original "Zelda DX"
- **Armageddon Games**: producers of Zelda Classic
- **Wild Bill**: founder of PureZC.net

### Zelda Classic development team

As listed in the game's own credits:

Dark Nation, DarkDragon, Deviance, fatcatfan, Gleeok, jman2050, Joe123, \_L\_, pkmnfrk, Saffith, Takuya

### ZQuestClassic

Current main developers: **Emily** (EmilyV99) and **Connor Clark** (connorjclark)

Contributors listed by the ZQuestClassic project:

Deathrider, Deedee, Etienne Vouga (evouga), Glenn, grayswandir, KoolAidWannaBe, Lexi Mayfield (leximayfield), Meroune Lorelie, Moosh (Moosh64), NightmareJames, P-Tux7, Peter Hull, SyrianBallaS, Tamamo No Mae, teilyr, Timelord

### GitHub contributors

To the [Zelda Classic](https://github.com/ArmageddonGames/ZeldaClassic) and [ZQuestClassic](https://github.com/ZQuestClassic/ZQuestClassic) repositories, beyond the names above:

arceusplayer11, Jambu2024, kasryan, Nekohime89, pedro-w, PlanNineFromOuterSpace, Rekiron, Renkoto, ShinjiniGuha07, thican, wilsonpe66, zeromus, ZoriaRPG

### Also credited in the changelogs and source

| Name | Credited for |
|---|---|
| koopa, pikaguy900 | Changelog entries |
| \*b\* | Intro picture |
| AmazingAmpharos, zcAmazing | Default quest and template fixes |
| BigJoe | Winning 5th Quest contest entry |
| Dan Furst | Script for the 1st Quest |
| Lut | Bug reports |
| Pokegamer1989 | Magic meter tiles and template fixes |
| Midnight | Original code behind the intro logo |

### ZC 3.0 Nightly intro

| Asset | By |
|---|---|
| ZQuest Classic logo | Eduardc |
| "ZC Forever" music | Beatscribe |

These come from the ZQuestClassic project under its own terms, which are included in `assets/zc/LICENSE.txt` with each release. They are not covered by the GPL.

### Libraries and tools

| Project | By |
|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | Sam Lantinga and the SDL contributors |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) and [glslang](https://github.com/KhronosGroup/glslang) | The Khronos Group |
| [volk](https://github.com/zeux/volk) | Arseny Kapoulkine |
| [Dear ImGui](https://github.com/ocornut/imgui) | Omar Cornut and contributors |
| [libnx](https://github.com/switchbrew/libnx) and the devkitPro Switch toolchain | switchbrew and devkitPro |
| [Mesa](https://www.mesa3d.org/) (OpenGL ES on Switch) | The Mesa contributors |
| [stb_vorbis](https://github.com/nothings/stb) | Sean Barrett |
| [minimp3](https://github.com/lieff/minimp3) | lieff |
| [Game_Music_Emu](https://github.com/libgme/game-music-emu) | Shay Green (blargg) and the libgme maintainers |
| [DUMB](https://github.com/kode54/dumb) | Ben Davis and contributors |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) | Bernhard Schelling |
| GeneralUser GS SoundFont | S. Christian Collins |
| [Allegro](https://liballeg.org/) | Shawn Hargreaves and the Allegro contributors, for the library Zelda Classic was built on and the reference build still uses |
| MD5 implementation | Colin Plumb |
| Flex and Bison (WinFlexBison) | Their respective authors |
| antimicro | Travis Nickles |

## License

Zelda Classic is released under the [GNU General Public License v3](license.txt). Bundled libraries keep their own licenses, found in their folders and in the `licenses` folder of each release.

*The Legend of Zelda* and Nintendo Switch are trademarks of Nintendo. This is an unofficial fan project, not affiliated with or endorsed by Nintendo.

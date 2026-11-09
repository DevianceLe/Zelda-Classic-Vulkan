## What's new

- **Linux build of the player** (64-bit). One download for nearly every current distribution: it needs glibc 2.30 or newer, the Vulkan loader and a Vulkan driver. ZQuest is still Windows only
- The window title now shows the quest's name and the ZC version it was built with
- Many more 2.55 and 3.0 Nightly scripts work. Scripts were reading 0 from most of the newer engine values, including `Distance()`, whenever no sprite was selected, and combo scripts couldn't read their own combo
- Fixed crashes in Mitches Fault II, Infinite Quest and I am mad day 2
- Quests with more combo favorites or tiles than the older limits now load correctly

## How far quests have been tested

Every quest in the PureZC collection was run through a test that visits every screen it uses: 795 of 843 quest files reached every screen. The test teleports Link around and doesn't play the quests through. Results for each quest are on the [compatibility page](https://github.com/DevianceLe/Zelda-Classic-Vulkan/blob/main/COMPATIBILITY.md).

## Still not working

- Slopes and z-heights, subscreen widget script commands, and saving global script variables past 255
- Script commands for bitmaps, palettes and lighting are skipped
- 14 quests stop at one screen in that test because a script never ends, which may not happen in normal play
- The player sometimes crashes as it closes
- Lifting, the switch hook, new dark rooms and some new combo types have not been confirmed in play
- Linux has been tested on Debian 13 and Arch Linux, in virtual machines with a software Vulkan driver, so real graphics cards and controllers are untested
- The Android app starts, but 2.55 and 3.0 Nightly quests have not been tested on Android yet

The full list is on the [compatibility page](https://github.com/DevianceLe/Zelda-Classic-Vulkan/blob/main/COMPATIBILITY.md#what-still-doesnt-work).

On Windows, unzip the download somewhere you can write to, then run `zplayer.exe` or `zqeditor.exe`. On Linux, unpack the `.tar.gz` and run `./zplayer` from inside the folder.

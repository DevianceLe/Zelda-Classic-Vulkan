## What's new

**Older quests**

- **Script drawing shows up again in 2.53 and older quests.** The 2.53 code this port started from pointed script drawing at a hidden image, so HUDs, script-drawn enemies and cutscenes in 2.50 and 2.53 quests were invisible. That was fixed in 2.53.1, and now here too
- Quests now play more like they did in the version they were made in. Each fix applies only to quests from that version:
  - 1.90 and early 1.92: "no warp" squares no longer act as wave warps, which broke maze walls and stairs in Puzzle Quest, Unification, The Revival and Eckland. Hearts stop at 16 unless the quest allows 24
  - 1.9x and 2.10: quests no longer start with 999 rupees when they only meant to allow carrying 999. Warp arrivals protect only the exact arrival spot. Each Moldorm, Lanmola and Manhandla segment can drop an item. Wizzrobes teleport anywhere. Door repair and money rooms renew. Sword swings check for secrets every frame (2.10)
  - 2.50.0 and 2.50.1: enemy weapons use the quest's own hitbox setting, script `Jump` values and a few early script commands give the old results, the global script runs after a screen scroll, Fire Octorok death shots home in, and Bombchu blasts use the enemy's weapon damage
- The A button no longer keeps an item from the last quest you played
- Crash guards for scripts that write outside the map or jump to a bad address, and a memory leak fixed when mirrors reflect weapons

**2.55 and 3.0 Nightly quests**

- **Slopes**, top-down and sideview: Link walks along and up them, drops through the ones that allow it, and enemies stand on them in sideview
- **Bottles**: buying in bottle shops, filling, drinking, and the fairy that revives you
- **Switch hook** works the way ZC 3.0 does: Link freezes, swaps halfway through, turns around, and swapped push blocks land properly, so they fill holes and open secrets. Swapping with enemies works too
- Push blocks fall into pits, and block triggers only look at the layers push blocks use
- **Sprite rotation, scaling and quarter-turn flips** from scripts, and shadows for weapons and items in the air
- **Subscreen widgets** that scripts can change, and **overlay subscreens**
- **Script lighting** and glowing sprites. Dark rooms use the quest's own dark colour, translucent edges and light shapes, including the lantern's cone
- Screen script data is kept in the save
- Many more script commands, and fixes for string formatting with variable arguments (used by 13 quests in the PureZC collection), `Game->LoadMapData` and others
- In quests using the newer movement rules, Link no longer sticks when walking along a wall or a floor
- Item pickups in 3.0 Nightly quests use the larger pickup box, as ZC 3.0 does
- Cut combos that change into the next combo no longer open secrets early

**Everything**

- On Windows, controllers no longer go through SDL's HIDAPI drivers, which were seen corrupting the game's memory. Xbox, PlayStation, Switch Pro, 8BitDo and most other pads use Windows' own controller drivers instead, with SDL's built-in button layouts. A pad that only works through HIDAPI (a Switch Pro Controller on USB, GameCube adapters, the Steam Controller) needs the new **Settings > Controls > Extra Pad Support** turned on. Linux and Android are unchanged
- A script that runs 30 million commands without waiting for a frame is now paused until the next frame instead of freezing the game, with a note in `allegro.log`
- Animated combos with tiles above 65,535 keep their tiles
- Crash reports are now written even when the player crashes while closing

## How far quests have been tested

Every quest in the PureZC collection was run through a test that visits every screen it uses. 818 of the 843 quest files reached every screen with no crash or hang, and 17 ran into the test's five-minute limit first. The rest are 6 files that aren't quest files, 1 damaged download and 1 quest with no screens. Six quests that used to run out of time now reach every screen, Mitches Fault II among them. The final run was stopped before its last 11 quests, so their results are from the build just before the final fixes.

2.53 quests are unchanged: the 1st, 2nd and 5th Quests and Dream's Realm re-save byte for byte, and a recorded 1st Quest replay matches all 2,478 frames.

## Still not working

- The Moon Pearl and bunny state, the magic mirror, the bug net, z-heights, and subscreens with more than one page (only the first page shows)
- Slopes, bottles, the switch hook, lighting and the new subscreen features have each been tried in only a few quests, and most of the older-quest fixes were checked through logs rather than seen in play
- The new Windows controller handling hasn't been tried with a real controller yet
- Lifting and some new combo types have not been confirmed in play
- Linux has been tested on Debian 13 and Arch Linux, in virtual machines with a software Vulkan driver, so real graphics cards and controllers are untested
- The Android app starts, but 2.55 and 3.0 Nightly quests have not been tested on Android yet
- The Switch build has only been tested in the Eden emulator, not on a real Switch

The full list is on the [compatibility page](https://github.com/DevianceLe/Zelda-Classic-Vulkan/blob/main/COMPATIBILITY.md#what-still-doesnt-work).

On Windows, unzip the download somewhere you can write to, then run `zplayer.exe` or `zqeditor.exe`. On Linux, unpack the `.tar.gz` and run `./zplayer` from inside the folder. On Switch, copy the `switch` folder from the zip to your SD card and start it from the Homebrew Menu.

## What's new in 2.53.444.6

**Runs without Vulkan**

- On a PC without Vulkan, or with a Vulkan driver that doesn't work, the player and ZQuest now draw through Direct3D (Windows), Metal (macOS) or OpenGL (Linux, Android) instead. The menus, dialogs and settings all still work. Before, the game showed its picture with no menus at all
- **Video Settings > Renderer** chooses yourself: Automatic (Vulkan first), Direct3D / OpenGL, or Software, which needs no graphics driver at all. The change takes effect the next time you start the program, and the page shows what's in use
- If Vulkan stops working while you play, the game switches over instead of losing its menus

**2.55 and 3.0 Nightly quests in the player**

- **Subscreens with more than one page:** pages turn with L and R, the page buttons and the page arrows quests put on them, with their slide and pixel animations and sounds. Each page keeps its cursor, and the subscreen reopens on the page you left. 3.0 widgets show and hide by their conditions (items, counters, levels, screens), and the cursor can stop on any selectable widget
- **The Moon Pearl and bunny form**, **the magic mirror** (warping to the mirror world and back, bouncing back off walls, water and pits, mirror portals) and **the bug net**, which catches items and fairies into bottles
- **Item pickup messages** and shop messages. Items that open a screen's secrets when you pick them up, screen items that wait for the secrets, and items that come back
- **Lifting and throwing** works now. Before, the lift glove could never lift anything
- Items follow their own settings where 2.55 moved quest rules into items: sword beams off mirrors, melee hits from the wand, hammer and Cane of Byrna, fire that can't hurt you, boomerangs dragging items, the Quick Sword, diving, the lens, and more
- Each candle and book decides whether its light lasts in a dark room
- DMaps' new titles and arrival messages. The title no longer shows twice in Tales of Murdoch, and Hyrule World 2.0 shows "Ordon Province" at the start
- The fade-to-black screen wipe, and messages wrap words as 2.55 does
- Fixes: some quests lost each DMap's overlay and map subscreen settings while loading; message control codes with large values; a warp to a negative screen is cancelled as in 2.55

**ZQuest**

- **ZQuest on Linux and macOS**, new and hardly tried. Keep backups of your quests. JPEG images can't be imported there yet (PNG and BMP can)
- A 2.53 quest can't hold tile numbers past 65,519. Saving such a tile in 2.53 or an older format now stops with the reason instead of losing it, and Import Tiles stops at that limit
- Fix: on Linux and macOS, 1.90 quests read some door combos and strings wrong, and could crash on exit

**macOS**

- The player now starts. In 2.53.444.5.1 it stopped at "Failed Init", because macOS only allows windows on the program's main thread

## How far it's been tested

- **2.53 quests are unchanged:** the 1st, 2nd and 5th Quests and Dream's Realm re-save byte for byte as before, and a recorded 1st Quest run plays exactly the same on all 2,478 frames.
- All 93 2.55 and 3.0 Nightly quests in the PureZC collection load. ZQuest opens and saves every quest in the collection that loads (836) and reloads the same data.
- The new player features were each tried in one to four quests: Tales of Zelsteria, Tales of Murdoch, Tranquil Expression, Hyrule World 2.0, Kirby's Castle Epic, Link's Rise 2 DX, Nestastic Gaiden, The Deep, Face Hunt and Hero Mode.
- Running without Vulkan was tried on Windows (32-bit and 64-bit, player and ZQuest), with Vulkan hidden from the game: both came up on Direct3D 11 with their menus. Software drawing works too. Linux, macOS and Android haven't been tried without Vulkan.
- **macOS** runs only on GitHub's build machines, where the player starts and loads the 1st Quest. Windows on ARM and Linux on ARM are still only built there.

## Still not working

- Z-heights
- Pages that a quest's own script turns (barato's card game) haven't been played through, and B and A items set on a later page haven't been checked after saving and loading
- Saving a 2.55 quest as 3.0 Nightly, editing the features only 2.55 and 3.0 Nightly have, and compiling 2.55 scripts
- Linux was tested only in virtual machines, Android hasn't been tried with 2.55 or 3.0 Nightly quests, and the Switch build only in the Eden emulator

The full list is on the [compatibility page](https://github.com/DevianceLe/Zelda-Classic-Vulkan/blob/main/COMPATIBILITY.md#what-still-doesnt-work).

**Which download?** On Windows, most PCs want `Windows-x64`; use `Windows-x86` on 32-bit Windows and `Windows-arm64` on ARM PCs. On Linux, `x86_64` for most PCs and `aarch64` for ARM boards and laptops. On a Mac, there's one download for every Mac.

On Windows, unzip the download somewhere you can write to, then run `zplayer.exe` or `zqeditor.exe`. On Linux and macOS, unpack the `.tar.gz` and run `./zplayer` or `./zqeditor` from inside the folder (on a Mac you can also double-click it, after the first-time step in its README). On Switch, copy the `switch` folder from the zip to your SD card and start it from the Homebrew Menu.

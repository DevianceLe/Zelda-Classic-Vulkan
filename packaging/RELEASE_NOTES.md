## What's new in 2.53.444.7

**3.0 quests: scrolling regions and Mystical Harps**

- **Scrolling regions** (3.0's "z3 scrolling"): in a region the camera follows Link instead of the screen changing at every edge. Every screen of the region is live: its enemies, items and FFCs move, run their scripts and block Link, nothing restarts when you cross an inner border, and the camera eases along as in 3.0. Darkness, light beams and the lens follow the camera, and screens you've been to show their opened secrets and chests
- Scripts see positions across the whole region, as in 3.0: Link, enemies, weapons, items, combos, FFCs, lights and `Screen->DrawOrigin`. In Mystical Harps the VulturPeak cart now stops Link where it should, and the rolling pins roll instead of flying off
- 2.55/3.0 quests that show the playing field's bottom 8 rows get the 232-row picture
- Script drawing as in 3.0: the script layers, draw origins and generic scripts (the temple's sky), the subscreen counters past 32, and Mystical Harps' HUD, A item and mermaid show through the opening
- An FFC script's `this` is its own FFC (it was always the screen's first): keys hidden by a script stay hidden, and Red Koopas walk
- Generic script events fire (game start, screen and DMap changes, hits, item pickups, enemy drops), and scripts run during warps and openings when the quest asks for it

**Items past 255**

- Items numbered 256 and up are real items (up to 1024; Mystical Harps has 616): you can pick them up and get them from enemies and special item rooms, and they show on the subscreen. Subscreen item slots follow 3.0's rules
- Items with their own size (wide or tall tiles, hit boxes, draw offsets) draw as in 3.0, and a 3.0 quest's items use 3.0's hit boxes (Mystical Harps no longer hands you the Quakes-Rod at the start of the cave)

**Messages and fonts**

- 2.55/3.0 messages as in 3.0: scrolling text, shadows, portraits, the more and scroll icons, translucency, and the message control codes (menus, screen states, warps, items, sounds)
- The 70 fonts 2.55 and 3.0 added are built in, so quests that use them show their own lettering (3.0's "Allegro" font shows as the default font)

**Speed and fixes**

- Tales of Murdoch, Passage Through Time and Destiny of the Oracles run at full speed again. A script error repeated every frame is written to the log at most once every 5 seconds, with a count
- Newer quests start in 3.0's order: Init, the hero's script, onLaunch and generic scripts run before the screen opens (Tranquil Expression's act cards appear)
- The DMap intro, the ending and scrolling clear the message box as 3.0 does

**ZQuest**

- **2.55 and 3.0 quests are edited with their own version's features**: combos (tabs for animation, attributes, flags, triggers, lifting and script), items, enemies, DMaps, screens, init data, quest rules (all of them, with upstream's help text), misc data, strings and subscreens
- **Save As 3.0 Nightly** for quests made in 2.55, and **Save As 2.55.17** for every quest made in an earlier 2.55 build, converted as upstream converts them: all 85 such quests in the PureZC collection come out byte for byte as upstream's own conversion (except the two whose names upstream keeps in a separate file). 3.0 Nightly prerelease quests save as 3.0
- A cleared message gets the default settings and a pasted one its source's; saving runs each part's writer once

**Switch hook** (2.55/3.0 quests): switching places with blocks, items, enemies and FFCs, in the instant, rise and flicker styles

**Tested:** 2.53 quests are unchanged: the 1st, 2nd and 5th quest and Dreams re-save byte for byte, and the recorded 1st Quest run plays the same on Windows and Linux (all 2,478 frames). All 93 2.55 and 3.0 Nightly quests in the PureZC collection load, tour without a crash and save back unchanged from ZQuest. Mystical Harps, Tranquil Expression and the others were compared screen by screen with ZC 3.0's own player. Windows 32-bit and 64-bit and Linux build.

**Not done yet:** in Mystical Harps the cloud layer sits a little off, dark rooms are brown where 3.0's are black, and the "GAME OVER" text is missing; a 3.0 quest's starting B item can differ from 3.0's (the bomb in the 1st Quest Master/Hero Mode remakes); ZQuest's Save As doesn't carry names kept in a separate `.zinfo` file.

## 2.53.444.6.1

**Gamepads just work**

- **The D-pad always moves Link**, on every pad. Many pads, and most PlayStation USB adapters, report their D-pad in a way 2.53 never read, so only the analog stick moved you, or nothing did
- **One button layout for every pad**, the one ZC's default buttons are made for: the right and bottom face buttons are A and B, Start is Start, Select opens ZC's menu, L and R are L and R, and the second shoulder buttons are Ex1 and Ex2. Before, Start was on the right-stick click of an Xbox or PlayStation pad. If you changed your joystick buttons in an earlier version, check them in Settings > Controls > Joystick Buttons
- **Any connected pad plays**: the game follows whichever pad you used last, so an adapter's empty second port or a virtual pad can no longer take its place
- **Settings > Controls > Gamepad Setup...** lists your pads and shows each button press as it reads it. A pad the game doesn't know (an old USB pad, a PlayStation adapter) can be set up there: press each button when asked, and it's remembered (in `gamecontrollerdb.txt` next to `zc.cfg`). After that it uses the standard layout in the game and in the menus. A community `gamecontrollerdb.txt` placed next to the program is read as well
- The menus can now be steered with any connected pad, not only the first

**2.55 and 3.0 Nightly quests**

- OneScreenGollab no longer runs out of script memory partway through: arrays an enemy's or weapon's script makes are freed with it, as in 2.55
- Script commands quests used that weren't there yet: `Bitmap->WriteTile` (Stellar Seas), polygons, `MaskedBlit`, the zap and wavy screen effects, `MakeAngular`/`MakeDirectional` for weapons, `NPC->NewDir`, the sprite list limits (`Game->MaxLWeapons` and the others, up to 255), and the mapdata tile-warp, side-warp and pattern values. A tour of all 93 2.55 and 3.0 Nightly quests in the PureZC collection now finds no script command or value the player doesn't handle
- `SaveSRAM()`/`LoadSRAM()` report failure to the script, as ZC 3.0 does

**Tested:** the 2.53 re-saves and the recorded 1st Quest run are unchanged (all 2,478 frames). Pads were tested with virtual devices, not a real controller: an unknown PlayStation-style adapter (D-pad, Start, set up and remembered across a restart, Grikarugun played with it) and a standard pad next to it. Windows 32-bit and 64-bit and Linux build.

**Grikarugun:** each menu ignores the buttons for about 5 seconds after it appears (that's the quest's own script), and its Z3 movement option leads back to the movement menu; choose Custom.

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

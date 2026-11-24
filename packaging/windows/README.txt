ZC Vulkan 2.53.444.12-PRE
Windows, 32-bit

The Zelda Classic 2.53 player and the ZQuest 2.53 quest editor, ported to SDL3
and Vulkan, with no Allegro. Both work with existing 2.53 quests, and the player
also plays quests made in 2.55 and the 3.0 Nightly builds. This is a new port,
so keep backups of any quest you care about.

  zplayer.exe        the game
  zqeditor.exe       ZQuest, the quest editor
  zscript255.exe     the 2.55.18 ZScript compiler (headers in zscript255_files)
  zscript30\         the 3.0 Nightly ZScript compiler and its headers

What's new
- Vulkan rendering, with window sizes, resolutions, scaling and fullscreen modes
- New Dear ImGui menus, dialogs and file browser, in the player and the editor
- ZQuest switches between its Large and Small layouts while it runs
  (Etc > Video Settings, or Etc > Options > Use Small Mode)
- ZQuest does not ask for quest passwords
- The player plays 2.55 and 3.0 Nightly quests (see "Newer quests" below)
- Quests can be kept in folders with # in the name
- X and Y item buttons in newer quests
- New startup intros: choose one in Settings > Intro

Running it
1. Unzip this folder somewhere you can write to (not Program Files).
2. Run zplayer.exe to play, or zqeditor.exe to edit quests.
3. The first time you run the player, it offers to create a save file: choose OK.

You need Windows 10 or 11 and a graphics driver with Vulkan support. Without
Vulkan the player still draws, but the new menus do not appear.

Playing your own quests
Put the .qst files in this folder. On the file select screen, register a name,
press A on its slot, and choose Custom Quest.

Newer quests
Quests made in 2.55 and the 3.0 Nightly builds play in zplayer.exe. This support
is new, and most testing so far has covered each quest's opening areas. Z-heights,
sideview swimming, slopes, lifting, the switch hook and the new dark rooms work,
but each has been tried in only a few quests.

Keep a quest's music beside its .qst file or in a Music folder next to it. Quest
scripts can only read and write files inside Files\<quest name> in this folder.

Editing quests
In ZQuest, use File > Open and browse to a .qst file. Etc > Video Settings has
the display options. The ZScript headers (std.zh, the std_zh folder, string.zh
and others) are included.

ZQuest opens quests of every version up to 3.0 Nightly and saves each in the
version it was made in (File > Save As can also save it as a newer version).
For 2.55 and 3.0 Nightly quests, Quest > Scripts > Compile ZScript runs that
version's own compiler: zscript255.exe for 2.55 quests and the one in the
zscript30 folder for 3.0 Nightly quests. Keep them beside zqeditor.exe.

Default player controls
  Arrows    Move
  Z         A button
  X         B button
  Enter     Start
  Space     Map
  A / S     L / R
  Q W C V   Ex1 - Ex4
  Esc       System menu
  Alt+Enter Toggle fullscreen (also in ZQuest)

Player settings are stored in zc.cfg and saved games in zc.sav. ZQuest settings
are stored in zquest.cfg.

Credits and licenses
Zelda Classic and ZQuest are by Armageddon Games and its contributors, and are
licensed under GPLv3 (licenses/ZeldaClassic-GPL3.txt). This port is by
Deviance. The complete source code is in the matching
ZC-2.53.444.12-PRE-Vulkan-Source.zip.

The ZC 3.0 Nightly intro uses the ZQuest Classic logo by Eduardc and the music
"ZC Forever" by Beatscribe. Their terms are in assets\zc\LICENSE.txt and are
separate from the GPL.

The game data files come from the Zelda Classic 2.53.1 Beta 55 package.

Includes SDL3, Dear ImGui, volk, Game_Music_Emu, DUMB, minimp3 and stb_vorbis.
Their licenses are in the licenses folder (stb_vorbis is public domain).

The Legend of Zelda is a trademark of Nintendo. This is an unofficial fan
project and is not affiliated with or endorsed by Nintendo.

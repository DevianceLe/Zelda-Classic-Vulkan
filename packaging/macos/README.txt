ZC Vulkan 2.53.444.6-PRE
macOS, Apple Silicon and Intel (one universal program)

HIGHLY EXPERIMENTAL. This macOS build is new and has only been built on
GitHub's build machines, never run on a real Mac. Expect problems, and keep
backups of your quests and saved games.

The Zelda Classic player, ported to SDL3 and Vulkan, with no Allegro. It plays
2.53 quests and quests made in 2.55 and the 3.0 Nightly builds. ZQuest, the
quest editor, is Windows only for now.

  zplayer              the game
  libMoltenVK.dylib    Vulkan for macOS (MoltenVK, runs on Apple's Metal)

What you need
  macOS 11 Big Sur or newer, on an Apple Silicon or an Intel Mac.

The first time
  This program isn't signed by Apple, so macOS stops it from opening. After you
  unpack the folder, open Terminal and run this once, with the folder's path:

    xattr -dr com.apple.quarantine ~/Downloads/ZC-2.53.444.6-PRE-Vulkan-macOS

  (Or Control-click zplayer, choose Open, then Open again.)

Running it
  Keep this folder somewhere you can write to, such as your Documents folder,
  not Applications. Double-click zplayer, or run it from Terminal inside the
  folder:

    ./zplayer -windowed -standalone "My Quest.qst"

  Put a quest's music next to the .qst file or in a Music folder beside it.

Keys
  Arrows          Move
  Z               A
  X               B
  Return          Start
  Space           Map
  Esc             System menu
  Option+Return   Toggle fullscreen

Player settings are stored in zc.cfg and saved games in zc.sav, in this folder.

Credits and licenses
Zelda Classic is by Armageddon Games and its contributors, and is licensed under
GPLv3 (licenses/ZeldaClassic-GPL3.txt). This port is by Deviance. The complete
source code is in the matching ZC-2.53.444.6-PRE-Vulkan-Source.zip.

MoltenVK is by The Brenwill Workshop and the Khronos Group, under the Apache
License 2.0 (licenses/MoltenVK.txt).

The ZC 3.0 Nightly intro uses the ZQuest Classic logo by Eduardc and the music
"ZC Forever" by Beatscribe. Their terms are in assets/zc/LICENSE.txt and are
separate from the GPL.

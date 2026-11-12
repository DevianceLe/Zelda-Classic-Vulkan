ZC Vulkan 2.53.444.7-PRE
Linux, 64-bit

The Zelda Classic player and the ZQuest quest editor, ported to SDL3 and
Vulkan, with no Allegro. The player plays 2.53 quests and quests made in 2.55
and the 3.0 Nightly builds. ZQuest opens quests of every version, saves each in
the version it was made in, and can save older quests as 2.55.17 or 3.0
Nightly. ZQuest on Linux is new: keep backups of your quests.

  zplayer    the game
  zqeditor   ZQuest, the quest editor

What you need
  A 64-bit Linux with glibc 2.30 or newer, which covers nearly every current
  distribution. Sound goes through PipeWire, PulseAudio or ALSA, whichever
  your system has, and the window works on X11 or XWayland.
  Vulkan is used when the Vulkan loader and a Vulkan driver for your graphics
  card are installed, for example:
    Debian, Ubuntu:  sudo apt install libvulkan1 mesa-vulkan-drivers
    Arch:            sudo pacman -S vulkan-icd-loader, plus your card's driver
  Without them it runs on OpenGL, or with software drawing as a last resort
  (Video Settings > Renderer lets you choose).

Running it
  Unpack this folder somewhere you can write to, then run ./zplayer (or
  ./zqeditor to edit quests) from inside it. To play a quest directly:

    ./zplayer -windowed -standalone "My Quest.qst"

  JPEG images can't be imported into ZQuest on Linux yet (PNG and BMP can).

  Put a quest's music next to the .qst file or in a Music folder beside it.

Keys
  Arrows    Move
  Z         A
  X         B
  Enter     Start
  Space     Map
  Esc       System menu
  Alt+Enter Toggle fullscreen

Player settings are stored in zc.cfg and saved games in zc.sav.

Tested so far on Debian 13 and Arch Linux, with Mesa's software Vulkan driver
in virtual machines. Real graphics cards and game controllers are untested on
Linux.

Credits and licenses
Zelda Classic is by Armageddon Games and its contributors, and is licensed under
GPLv3 (licenses/ZeldaClassic-GPL3.txt). This port is by Deviance. The complete
source code is in the matching ZC-2.53.444.7-PRE-Vulkan-Source.zip.

The ZC 3.0 Nightly intro uses the ZQuest Classic logo by Eduardc and the music
"ZC Forever" by Beatscribe. Their terms are in assets/zc/LICENSE.txt and are
separate from the GPL.

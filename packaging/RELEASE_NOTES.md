## What's new

**New builds: 64-bit Windows, ARM and Mac (highly experimental)**

- **Windows, 64-bit (x64):** the player and ZQuest. The 32-bit Windows download is still here as well
- **Windows on ARM (ARM64):** the player and ZQuest, for Snapdragon and other ARM PCs
- **Linux on ARM (aarch64):** the player
- **macOS:** one player for both Apple Silicon and Intel Macs, on macOS 11 Big Sur or newer. Vulkan comes through MoltenVK, which is included. The program isn't signed by Apple, so the first time you have to let macOS open it; the README in the download says how. ZQuest is still Windows only
- Quests behave the same in every build. On a Mac the player uses the same code as on Linux, not 2.53's old Mac code, which would have read some quest data in the wrong byte order

**Fixes**

- macOS: the check for a second copy of the player wrote past a buffer, because macOS keeps a shorter socket path than Linux. Linux is unchanged

## How far it's been tested

- **Windows 64-bit** was built and checked on Windows 11. The 64-bit player plays a recorded 1st Quest run exactly as the 32-bit one does, all 2,478 frames. 64-bit ZQuest re-saves the 1st, 2nd and 5th Quests and Dream's Realm byte for byte, the same as the original 2.53.
- **Windows on ARM and Linux on ARM** are only built, on GitHub's build machines. Neither has been run yet.
- **macOS** is only built on GitHub's build machines, as one program for both kinds of Mac. It hasn't been run on a real Mac yet.
- Everything in 2.53.444.4 is unchanged: ZQuest still edits quests of every version and saves older quests as 2.55.17 or 3.0 Nightly.

## Still not working

- Everything on the new builds beyond what's listed above is untested. Please report what you find
- ZQuest is only built for Windows. Linux, macOS and the Switch get the player only
- Saving a 2.55 quest as 3.0 Nightly, editing the features only 2.55 and 3.0 Nightly have, and compiling 2.55 scripts
- The player's open items are unchanged: the Moon Pearl and bunny state, the magic mirror, the bug net, z-heights, and subscreens with more than one page
- Linux was tested only in virtual machines, Android hasn't been tried with 2.55 or 3.0 Nightly quests, and the Switch build only in the Eden emulator

The full list is on the [compatibility page](https://github.com/DevianceLe/Zelda-Classic-Vulkan/blob/main/COMPATIBILITY.md#what-still-doesnt-work).

**Which download?** On Windows, most PCs want `Windows-x64`; use `Windows-x86` on 32-bit Windows and `Windows-arm64` on ARM PCs. On Linux, `x86_64` for most PCs and `aarch64` for ARM boards and laptops. On a Mac, there's one download for every Mac.

On Windows, unzip the download somewhere you can write to, then run `zplayer.exe` or `zqeditor.exe`. On Linux and macOS, unpack the `.tar.gz` and run `./zplayer` from inside the folder (on a Mac you can also double-click it, after the first-time step in its README). On Switch, copy the `switch` folder from the zip to your SD card and start it from the Homebrew Menu.

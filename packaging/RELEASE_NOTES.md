## What's new

**ZQuest edits quests of every version**

- **ZQuest opens quests from every version**, 1.90 through 2.55 and 3.0 Nightly, and **saves each one in the version it was made in.** Parts you didn't change are kept exactly as they were in the file, and the parts you changed are written in that version's own format. A 2.10 quest stays a 2.10 quest, and a 3.0 Nightly quest stays a 3.0 Nightly quest
- **Save As can save an older quest as a newer version**: 2.53.1, 2.55.17 or 3.0 Nightly. Choose **File > Save As**, then **As a newer version**, then the version. A file saved as 2.55.17 or 3.0 Nightly can't be opened by older versions of ZC any more, so ZQuest asks before it saves. It never saves a quest as a version older than the one it was made in
- A change the version can't hold stops the save, and ZQuest says what it was. Examples are a combo type 2.10 doesn't have, a string longer than an old version stores, or a rule that version always resets. The file is left as it was
- A save writes to a temporary file first and only replaces the quest once the whole save has worked
- ZQuest holds as many tiles as the player, 214,500, for 2.55 and 3.0 Nightly quests. 2.53 and older quests stay within the 65,520 tiles their files can store
- The window title shows the version the quest was built with, as the player's does
- Fixed a crash that could happen as ZQuest closed. ZQuest's double-click timer could copy the screen while it was being freed

**Both programs**

- Fixed memory corruption while loading quests with empty text on a custom subscreen. It came from the 2.53 code this port started from, and it made ZQuest or the player crash now and then while loading
- Fixed a crash when a quest has a sound name exactly 36 characters long
- Some 2.55 and 3.0 Nightly files that the real ZC opens failed to open here. They open now
- A quest that fails to load shows its error at once instead of waiting for a key

## How far it's been tested

- **Saving in the quest's own version:** all 836 quests in the PureZC collection that load were opened and re-saved with every part rewritten. Every part reloads with the same data.
- **Saving as a newer version:** all 743 quests in the collection made in 2.53.1 or older were saved as 2.55.17 and as 3.0 Nightly. Each file was compared, section by section, with the real ZC 2.55.17's or 3.0 Nightly's own conversion of the same quest. Every section is identical apart from the header's build details. For 2.55.17 the weapon-sprite data also differs in one field, because 2.55.17 fills that field with leftover memory when it converts an older quest. ZQuest writes 0 there, as 3.0 does. Every saved file opens in the real 2.55.17 or 3.0 player and in this one. A sample of 213 was also opened and saved again by the real 2.55.17 or 3.0 ZQuest.
- **Through ZQuest's own window:** a quest from each version (1.90, 1.92, 2.10, 2.11, 2.50, 2.53.0 and 2.53.1) had a combo placed on a screen and was saved through File > Save As as 2.55.17 and as 3.0 Nightly. Each saved file matched the real ZC's conversion apart from that screen. It reopened unchanged, the real ZC's ZQuest and player opened it, and this player visited every screen.
- 2.53 quests are unchanged: the 1st, 2nd and 5th Quests and Dream's Realm re-save byte for byte, and a recorded 1st Quest replay matches all 2,478 frames.

## Still not working

- Saving a 2.55 quest as 3.0 Nightly
- Editing the features only 2.55 and 3.0 Nightly have, and compiling 2.55 scripts. ZQuest keeps the compiled scripts a 2.55 or 3.0 quest came with
- A rule a newer version always resets (rule 157 in 2.50 quests) can't be changed in a save as that version. ZQuest says so and saves nothing
- ZQuest is only built for Windows. Linux and the Switch get the player only
- The player's open items are unchanged since 2.53.444.3: the Moon Pearl and bunny state, the magic mirror, the bug net, z-heights, and subscreens with more than one page
- Linux was tested only in virtual machines, Android hasn't been tried with 2.55 or 3.0 Nightly quests, and the Switch build only in the Eden emulator

The full list is on the [compatibility page](https://github.com/DevianceLe/Zelda-Classic-Vulkan/blob/main/COMPATIBILITY.md#what-still-doesnt-work).

On Windows, unzip the download somewhere you can write to, then run `zplayer.exe` or `zqeditor.exe`. On Linux, unpack the `.tar.gz` and run `./zplayer` from inside the folder. On Switch, copy the `switch` folder from the zip to your SD card and start it from the Homebrew Menu.

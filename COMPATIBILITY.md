# Quest compatibility

Every quest in the PureZC collection, 843 quest files, run through the player on Windows on ZC Vulkan 2.53.444.3.

**How to read this.** Each quest is loaded and started on a fresh save. A test then teleports Link to every screen the quest uses, stays about a second and a half on each, moves and presses buttons, and opens the subscreen now and then. Scripts, enemies and screen changes all run.

**All screens visited** means every one of those screens loaded and ran without a crash or a hang. It does not mean the quest can be finished. The test doesn't solve puzzles, fight bosses, enter caves or follow the story, sound is off, and it only runs for up to five minutes per quest.

**Partly visited** means the five minutes ran out before the last screen. The run itself didn't fail, but the screens it didn't reach are untested.

**Scripts that never end.** Teleporting can put a script in a situation no player would reach, and some scripts then loop forever without waiting for the next frame. The test pauses such a script so it can carry on, so a quest listed here can still freeze if a player gets into that same situation.

The final run was stopped before its last 11 quests. Their results are from the same test on the build just before the final fixes.

| Result | Quests |
|---|---|
| All screens visited | 818 |
| Partly visited (time limit) | 17 |
| Refused: not a recognised quest file | 6 |
| Download is damaged | 1 |
| Runs; no screens to visit | 1 |
| **Total** | **843** |

| Made in | All screens visited | Partly visited | Other |
|---|---|---|---|
| 1.90 | 41 | 0 | 0 |
| 1.92 | 75 | 1 | 0 |
| 2.10 | 165 | 0 | 0 |
| 2.11 | 6 | 0 | 0 |
| 2.50 beta | 27 | 0 | 0 |
| 2.50.0 | 143 | 1 | 0 |
| 2.50.1 | 65 | 0 | 0 |
| 2.50.2 | 103 | 5 | 0 |
| 2.50.3 | 6 | 0 | 0 |
| 2.53.0 | 13 | 0 | 0 |
| 2.53.1 | 86 | 6 | 0 |
| 2.55 or 3.0 Nightly | 88 | 4 | 1 |
| unknown | 0 | 0 | 7 |

## What still doesn't work

**Not built yet**

- The Moon Pearl and the bunny state, and the magic mirror item. Kirby's Castle Epic needs the bunny for one of its levels.
- Z-heights. No quest in this collection uses them yet.
- Subscreens with more than one page show only the first page. Twelve quests in this collection have such subscreens, Mystical Harps and Tranquil Expression among them.
- The bug net, so fairies can't be caught in bottles.
- `Game->IncrementQuest`, which a 2.53.1 script uses to go straight on to the next numbered quest.
- A script-moved viewport, and saving global script variables past 255. No quest in this collection needs the second one.
- Quests made in ZC 1.00 and 1.50 can't be opened. None are in this collection.
- The 2.55 and 3.0 Nightly editor. ZQuest refuses those quests until it exists, and on Linux there's no ZQuest yet at all.

**Built, but tried in only a few quests**

- Slopes: tried in Link's Grand Adventure 3 Remastered and Novel Ruins Gaiden. Enemies on slopes and moving slopes haven't been seen working.
- Bottles and bottle shops: tried in Link's Journey North, The Deep and Mystical Harps. Saving and reloading bottle contents hasn't been tried.
- Switch hook: tried in Face Hunt, Cambria, Infinite Quest and Jack's Quest. The hook's chain disappears while the switch happens, switched combos and enemies don't flicker or rise, and switching with FFCs isn't done.
- Push blocks falling into pits: tried in Colossal Tower.
- Sprite rotation and scaling, script lighting, subscreen widgets and overlay subscreens. Shadows under flying weapons and items haven't been seen on screen yet.
- Older quests: many fixes this release bring back how their own version played (1.9x warps and the 16-heart cap, 2.10 warp arrivals, dropping segments, Wizzrobes and renewing rooms, 2.50.0 and 2.50.1 enemy weapon sizes and script values). They were checked through logs and recordings; most haven't been seen happening in play.

**Written, but not yet seen working in real play**

- Lifting and throwing, torches and lantern
- Ice, new mirrors, screen tints, light beams, bridges and pitfalls
- Push blocks on layers, scripts running through screen scrolls, passive item scripts, and secret-screen rules

**Known bugs**

- On Windows, SDL's HIDAPI controller drivers were seen writing over the game's memory while listing devices. That code is inside SDL, so on Windows the game now leaves it off: Xbox, PlayStation, Switch Pro, 8BitDo and most other pads go through Windows' own controller drivers instead, with SDL's built-in button layouts. Pads that only work through HIDAPI (a Switch Pro Controller on USB, GameCube adapters, the Steam Controller) need **Settings > Controls > Extra Pad Support** turned on, which brings the risk back. That has not yet been tried with a real controller.
- A script that runs 30 million commands without waiting for a frame is paused until the next frame, so the game no longer freezes. A note goes to `allegro.log` when that happens. The quest may still not behave as its author meant at that spot.
- The "Flip-Flop Cancel and Wave Warps" option now undoes the 1.9x warp fix if you turn it on. Leave it off.

**Platforms**

- Linux has been tested on Debian 13 and Arch Linux, in virtual machines with Mesa's software Vulkan driver. Real graphics cards and controllers are untested. EndeavourOS froze while booting in the test virtual machine, before the game could run, so it hasn't been tested.
- Android starts, but 2.55 and 3.0 Nightly quests haven't been tested on it.
- The Nintendo Switch homebrew build has only been tested in the Eden emulator, not on a real Switch. ZQuest isn't part of it.

## Played by hand

**2.55 and 3.0 Nightly quests, in the player**

| Quest | Format | What has been played |
|---|---|---|
| Mystical Harps (T.L.O.Z. IV) | 3.0 Nightly | Intro, title, new game, first areas, walking, scrolling and swimming, with its full music folder |
| Tranquil Expression | 3.0 Nightly | Full opening, title, house scene and dialogue, item screen |
| Herald of Calamity (Demo) | 2.55 | Title, gameplay, script-driven enemies, combat, item screen, save and reload |
| Cambria: The Lost Worlds | 2.55 | Opening |
| Shadow Knight | 2.55 | Opening |
| Alarm Clock (ZScript example) | 2.55 | Clock shows the real time |

Every section of these six quest files reads without errors. They are mostly opening-area tests, so none has been played to the end.

**2.53 and older quests**

| Quest | Format | Result |
|---|---|---|
| 1st Quest | 2.53 | Identical frames across a 2,478-frame replay; re-save byte-identical |
| 2nd Quest | 2.53 | Re-save byte-identical |
| 5th Quest | 2.53 | Re-save byte-identical |
| Dream's Realm | 2.53 | Re-save byte-identical; loads on Windows; plays into its opening on Android |
| The Legend of Revali | 2.53 | Loads and starts; frame comparison not finished |
| De Legend uh Zelda: Fall uh de Honky Pig | 1.90 | Plays |

The 3rd and 4th Quests have not been tested on this port yet.

## All quests

Pack entry is the name of the download from [PureZC](https://www.purezc.net/index.php?page=quests). Screens is how many of the quest's screens the test reached.

| Pack entry | Quest file | Made in | Result | Screens |
|---|---|---|---|---|
| q1 | OcarinaOfPower.qst | 1.90 | All screens visited | 396 of 396 |
| q2 | HonkyPig.qst | 1.90 | All screens visited | 398 of 398 |
| q3 | CFPDemo3.qst | 1.92 | All screens visited | 517 of 517 |
| q4 | The Revenge.qst | 1.90 | All screens visited | 304 of 304 |
| q5 | The Revenge2.qst | 1.90 | All screens visited | 603 of 603 |
| q6_Unification | unification - original.qst | 1.90 | All screens visited | 423 of 423 |
| q6_Unification | unification.qst | 1.90 | All screens visited | 423 of 423 |
| q7 | turnabout.qst | 1.92 | All screens visited | 475 of 475 |
| q8 | FragmentsofPower.qst | 1.92 | All screens visited | 476 of 476 |
| q9 | puzzlequest.qst | 1.90 | All screens visited | 456 of 456 |
| q10 | cgbzfirst.qst | 1.92 | All screens visited | 383 of 383 |
| q11 | loz_Triforceofcourage.qst | 1.90 | All screens visited | 421 of 421 |
| q13 | Warrior'sRise163D.qst | 1.92 | All screens visited | 225 of 225 |
| q14 | Eckland.qst | 1.92 | All screens visited | 583 of 583 |
| q15 | PureQuest.qst | 1.92 | All screens visited | 884 of 884 |
| q16 | Iiad.qst | 1.92 | All screens visited | 516 of 516 |
| q17 | Shadowofevil(1.92v).qst | 1.92 | All screens visited | 892 of 892 |
| q18 | Death Mountain.qst | 1.90 | All screens visited | 394 of 394 |
| q19 | TriforcesCurse.qst | 1.90 | All screens visited | 428 of 428 |
| q21 | Doublevision.qst | 1.92 | All screens visited | 818 of 818 |
| q23 | TheFragmentsofPower.qst | 1.92 | All screens visited | 476 of 476 |
| q24 | Ganons Reign.qst | 1.92 | All screens visited | 241 of 241 |
| q25 | LandsofSerenity.qst | 2.10 | All screens visited | 1084 of 1084 |
| q26 | lttf-fixed.qst | 2.50.0 | All screens visited | 771 of 771 |
| q27_LinksBirthday | Link's Birthday 250.qst | 2.50.0 | All screens visited | 688 of 688 |
| q29 | Gerudos Rise to Power.qst | 1.90 | All screens visited | 163 of 163 |
| q30 | Geepas First Quest.qst | 1.92 | All screens visited | 152 of 152 |
| q33 | questmaze.qst | 1.90 | All screens visited | 132 of 132 |
| q34 | HID5.qst | 1.92 | All screens visited | 468 of 468 |
| q36 | quests-OflM8-Jimmy Beat World.qst | 1.90 | All screens visited | 630 of 630 |
| q37 | HoT182.qst | 1.92 | All screens visited | 714 of 714 |
| q38 | webonus.qst | 1.92 | All screens visited | 455 of 455 |
| q38 | webonuseasy.qst | 1.92 | All screens visited | 455 of 455 |
| q39 | quests-0prfF-NewSecond.qst | 1.92 | All screens visited | 377 of 377 |
| q46 | quests-ZSPdj-medalion.qst | 1.90 | All screens visited | 388 of 388 |
| q47 | quests-1072605809-package-Zelda3000.qst | 1.92 | All screens visited | 353 of 353 |
| q48 | quests-swCTN-3000-1st.qst | 1.90 | All screens visited | 367 of 367 |
| q49 | King - King Quest.qst | 1.90 | All screens visited | 491 of 491 |
| q52 | HiddenDualityDC.qst | 1.92 | All screens visited | 1896 of 1896 |
| q55 | MegaManData.qst | 1.90 | All screens visited | 322 of 322 |
| q56 | Zelda-Ages.qst | 1.90 | All screens visited | 1011 of 1011 |
| q57 | MegaTurb.qst | 1.90 | All screens visited | 577 of 577 |
| q60 | King2.qst | 1.90 | All screens visited | 566 of 566 |
| q61 | DeadLand.qst | 1.90 | All screens visited | 645 of 645 |
| q62 | thugInHyrule.qst | 1.92 | All screens visited | 184 of 184 |
| q64_LoZTheQuestThroughTime | Timer.qst | 1.90 | All screens visited | 231 of 231 |
| q64_LoZTheQuestThroughTime | TIMERFIX.QST | 1.90 | All screens visited | 232 of 232 |
| q65_HowHigh | HowHigh.qst | 1.90 | All screens visited | 548 of 548 |
| q66 | vargen.qst | 1.90 | All screens visited | 430 of 430 |
| q67 | MegaManDWR.qst | 1.92 | All screens visited | 5068 of 5068 |
| q68 | escapehyrule.qst | 1.90 | All screens visited | 318 of 318 |
| q69 | 4th.qst | 1.92 | All screens visited | 308 of 308 |
| q70 | 5th.qst | 1.92 | All screens visited | 420 of 420 |
| q72 | lightworld.qst | 1.92 | All screens visited | 579 of 579 |
| q73 | thequest.qst | unknown | Refused: not a recognised quest file |  |
| q75_TheRealmofShamanock | Realm_Shamanock.qst | 1.90 | All screens visited | 794 of 794 |
| q78 | quests-1088525868-package-ZCchapter1.qst | 1.90 | All screens visited | 358 of 358 |
| q79 | ZCchapter2.qst | 1.90 | All screens visited | 351 of 351 |
| q80 | ZCchapter3.qst | 1.90 | All screens visited | 325 of 325 |
| q81 | (archive) | unknown | Download is damaged |  |
| q82 | 6thb.qst | 1.92 | All screens visited | 395 of 395 |
| q83_NewQuest | new.qst | 1.92 | All screens visited | 441 of 441 |
| q84 | IsleoftheWinds.qst | 1.92 | All screens visited | 1294 of 1294 |
| q86 | Legend of Zelda_Origins 1st Quest.qst | 1.92 | All screens visited | 486 of 486 |
| q87_TheRealmofShamanockDX | Shamanock_dx.qst | 1.92 | All screens visited | 789 of 789 |
| q88_JamesQuest | james.qst | 1.92 | All screens visited | 777 of 777 |
| q89 | 5thDX.qst | 1.92 | All screens visited | 419 of 419 |
| q90 | Gina's Journey.qst | 1.90 | All screens visited | 246 of 246 |
| q91 | Legend of ZeldaGanons Return v25.qst | 1.92 | All screens visited | 737 of 737 |
| q92 | HADHAD.qst | 1.90 | All screens visited | 197 of 197 |
| q93 | Bikdips Adventure.qst | 1.90 | All screens visited | 420 of 420 |
| q94 | Mitch.qst | 1.90 | All screens visited | 530 of 530 |
| q95 | Gina's Journey 2.qst | 1.90 | All screens visited | 356 of 356 |
| q96 | new2.qst | 1.92 | All screens visited | 542 of 542 |
| q97 | LOZ_Origins 2nd Quest v3.8a.qst | 1.92 | All screens visited | 806 of 806 |
| q98 | Master Quest.qst | 1.92 | All screens visited | 855 of 855 |
| q99 | MegaManDWR_DC.qst | 1.92 | Partly visited (time limit) | 4849 of 5908 |
| q100 | nemel.qst | 1.92 | All screens visited | 2274 of 2274 |
| q102 | HIC2.qst | 1.92 | All screens visited | 505 of 505 |
| q103 | TheTaoOfLink.qst | 1.92 | All screens visited | 992 of 992 |
| q104_DemoQuestEX | demoex25.qst | 2.53.0 | All screens visited | 582 of 582 |
| q105 | LB-DX.qst | 1.92 | All screens visited | 1081 of 1081 |
| q107 | vargen_improved.qst | 2.10 | All screens visited | 464 of 464 |
| q108 | Evil Castle.qst | 2.10 | All screens visited | 164 of 164 |
| q109 | molka.qst | 1.92 | All screens visited | 659 of 659 |
| q110 | Armageddon.qst | 1.92 | All screens visited | 515 of 515 |
| q111 | quests-1118966227-package-ABITHF.qst | 2.10 | All screens visited | 456 of 456 |
| q113 | Legendofthethreeelements.qst | 2.10 | All screens visited | 736 of 736 |
| q114 | Hyrule.qst | 1.92 | All screens visited | 270 of 270 |
| q115 | Legend of Zelda Ganons Revenge .qst | 2.10 | All screens visited | 308 of 308 |
| q116 | Z.qst | 2.10 | All screens visited | 159 of 159 |
| q117 | Lady of Dreams(Hybrid_Link).qst | 2.10 | All screens visited | 456 of 456 |
| q117 | Lady of Dreams(Pure_Link).qst | 2.10 | All screens visited | 456 of 456 |
| q119 | zeroquest.qst | 2.10 | All screens visited | 417 of 417 |
| q120 | return.qst | 2.10 | All screens visited | 587 of 587 |
| q121 | The Ship.qst | 2.10 | All screens visited | 112 of 112 |
| q122_LinksRetirement | Links_Retirement.qst | 2.50.1 | All screens visited | 900 of 900 |
| q123 | MetroidLegends.qst | 2.10 | All screens visited | 344 of 344 |
| q124 | The Power of Seasons.qst | 2.10 | All screens visited | 584 of 584 |
| q125 | zelda_quest.qst | 2.10 | All screens visited | 1514 of 1514 |
| q126 | ArenaFightL.qst | 2.10 | All screens visited | 1364 of 1364 |
| q127 | quests-1158620002-package-crossrealm.qst | 2.10 | All screens visited | 534 of 534 |
| q128 | mqptone.qst | 2.10 | All screens visited | 1479 of 1479 |
| q129 | LoL-GC.qst | 1.92 | All screens visited | 517 of 517 |
| q130 | Destiny.qst | 2.10 | All screens visited | 418 of 418 |
| q131 | quests-1139864852-package-Dark Advent.qst | 2.10 | All screens visited | 50 of 50 |
| q132 |  9 volt.qst | 2.10 | All screens visited | 497 of 497 |
| q133 | quests-1140991359-package-HeroStory.qst | 1.92 | All screens visited | 598 of 598 |
| q134 | STAGES.qst | 2.10 | All screens visited | 193 of 193 |
| q135 | tlolahrtsfinal.qst | 1.92 | All screens visited | 1074 of 1074 |
| q136 | tlolahats.qst | 1.92 | All screens visited | 1794 of 1794 |
| q137 | overunder.qst | 2.10 | All screens visited | 31 of 31 |
| q138 | Hyrule's Darkness.qst | 2.10 | All screens visited | 401 of 401 |
| q139_TheHeroofDreams | HeroOfDreams.qst | 2.10 | All screens visited | 5190 of 5190 |
| q140 | ShadowWars.qst | 2.10 | All screens visited | 388 of 388 |
| q141 | Metroid Zebe Galaxy .qst | 2.10 | All screens visited | 61 of 61 |
| q144 | The missin.qst | 2.10 | All screens visited | 694 of 694 |
| q146 | Quest On Part I.qst | 1.90 | All screens visited | 270 of 270 |
| q146 | Quest On Part II.qst | 1.90 | All screens visited | 272 of 272 |
| q147 | Doom spikes.qst | 2.10 | All screens visited | 1 of 1 |
| q148_LinksRetirement3AStitchinTime | link3.qst | 2.50.1 | All screens visited | 1155 of 1155 |
| q149 | the legend of zelda the link travel in the time.qst | 2.10 | All screens visited | 129 of 129 |
| q150 | qst4hyrule3.qst | 2.10 | All screens visited | 1097 of 1097 |
| q151 | Mulder.qst | 2.10 | All screens visited | 108 of 108 |
| q152 | cashaunt2.qst | 2.10 | All screens visited | 547 of 547 |
| q153 | cashaunt.qst | 1.92 | All screens visited | 289 of 289 |
| q154 | Swansong.qst | 1.92 | All screens visited | 709 of 709 |
| q155 | SearchE.qst | 1.92 | All screens visited | 1394 of 1394 |
| q156 | QFreedom.qst | 1.90 | All screens visited | 521 of 521 |
| q157 | QFreedomII.qst | 1.92 | All screens visited | 1293 of 1293 |
| q158 | tortuga.qst | 2.10 | All screens visited | 1442 of 1442 |
| q159 | Elements.qst | 2.10 | All screens visited | 234 of 234 |
| q160 | Shadowfall.qst | 2.10 | All screens visited | 108 of 108 |
| q162 | TheTaoOfLinkDC.qst | 2.10 | All screens visited | 992 of 992 |
| q164 | Link's bath.qst | 2.10 | All screens visited | 568 of 568 |
| q165 | Kendrus.qst | 2.10 | All screens visited | 730 of 730 |
| q166 | Mini Quest.qst | 2.10 | All screens visited | 35 of 35 |
| q167 | ArmageddonForever.qst | 1.92 | All screens visited | 543 of 543 |
| q169 | Switch Example.qst | 2.10 | All screens visited | 4 of 4 |
| q171 | pyramid quest.qst | 2.10 | All screens visited | 76 of 76 |
| q172 | 100yrs.qst | 2.10 | All screens visited | 396 of 396 |
| q173 | RaftMountainsQuest2.1LinksBigR-tlseward.txt.qst | 2.10 | All screens visited | 721 of 721 |
| q174 | kingsroad.qst | 2.10 | All screens visited | 473 of 473 |
| q175 | BOD.qst | 2.10 | All screens visited | 547 of 547 |
| q176 | quests-1164316498-package-Titans Quest.qst | 2.10 | All screens visited | 1270 of 1270 |
| q177 | quests-1164574249-package-the Island.qst | 2.10 | All screens visited | 179 of 179 |
| q178 | TLOZ-POG.qst | 2.10 | All screens visited | 1036 of 1036 |
| q179 | Linkstech.qst | 2.10 | All screens visited | 400 of 400 |
| q180 | WolfStyleDX.qst | 1.92 | All screens visited | 823 of 823 |
| q181 | DeathofLink.qst | 2.10 | All screens visited | 625 of 625 |
| q182 | Worlds.qst | 2.10 | All screens visited | 1943 of 1943 |
| q183 | The legend of zelda Holodrums trouble.qst | 2.10 | All screens visited | 37 of 37 |
| q184 | zeldacallingnopass.qst | 2.10 | All screens visited | 646 of 646 |
| q185 | MisterMarioMan-QuestForRetards-2.10.qst | 2.10 | All screens visited | 346 of 346 |
| q186 | chibi.qst | 2.10 | All screens visited | 206 of 206 |
| q187 | lgp.qst | 1.92 | All screens visited | 252 of 252 |
| q188 | easyquest.qst | 2.10 | All screens visited | 245 of 245 |
| q189 | Miriam.qst | 2.10 | All screens visited | 208 of 208 |
| q190 | Death'sPower.qst | 2.10 | All screens visited | 534 of 534 |
| q191 | ZGDNWtQDH-EM.qst | 2.10 | All screens visited | 231 of 231 |
| q191 | ZGDNWtQDH-HM.qst | 2.10 | All screens visited | 231 of 231 |
| q191 | ZGDNWtQDH.qst | 2.10 | All screens visited | 233 of 233 |
| q192 | marios quest.qst | 2.10 | All screens visited | 10 of 10 |
| q193 | TheEightSpirits.qst | 2.10 | All screens visited | 758 of 758 |
| q194 | ProtectorOfTheDamnUniverse-TacoChopper.qst | 2.10 | All screens visited | 1032 of 1032 |
| q195 | MQ - New BS.qst | 1.92 | All screens visited | 648 of 648 |
| q196 | loz_thl.qst | 2.10 | All screens visited | 782 of 782 |
| q197 | Dasses Quest by Lars.qst | 2.10 | All screens visited | 220 of 220 |
| q198 | nulink.qst | 2.10 | All screens visited | 589 of 589 |
| q199 | quests-1184028557-package-subrosian.qst | 2.10 | All screens visited | 383 of 383 |
| q201_InsertQuestTitleHere | InsertQuestTitleHere.qst | 2.10 | All screens visited | 644 of 644 |
| q202 | GG.qst | 2.10 | All screens visited | 685 of 685 |
| q203 | Origin - DarkFlameWolf.qst | 2.50 beta | All screens visited | 4665 of 4665 |
| q204 | Lost_Isle.qst | 2.50 beta | All screens visited | 5315 of 5315 |
| q205 | dltt.qst | 2.10 | All screens visited | 646 of 646 |
| q206 | LinksGiantAdventureRevisedBytlseward.qst | 2.10 | All screens visited | 684 of 684 |
| q206 | LinksGiantAdventureRevisedBytlsewardNoPassword.qst | 2.10 | All screens visited | 684 of 684 |
| q207_LoZLinksDecision | LoZ - LD-easy.qst | 2.10 | All screens visited | 1166 of 1166 |
| q208 | quests-1188722503-package-1st.qst | 2.11 | All screens visited | 367 of 367 |
| q209 | HyrulesBigFlood-tlseward.qst | 2.10 | All screens visited | 669 of 669 |
| q210 | Quest for Mrock - The Goomba Guy.qst | 2.10 | All screens visited | 477 of 477 |
| q211 | journeyofhell.qst | 2.10 | All screens visited | 145 of 145 |
| q212 | Hyruleswamp.qst | 2.10 | All screens visited | 847 of 847 |
| q213 | cestria.qst | 2.10 | All screens visited | 682 of 682 |
| q214 | 3Isles-tlseward.qst | 2.10 | All screens visited | 765 of 765 |
| q215 | REAL TWO WEEK SPEED QUEST.qst | 2.10 | All screens visited | 219 of 219 |
| q216 | HyruleChristmasDX.qst | 2.10 | All screens visited | 314 of 314 |
| q217 | Link_to_the_Heavens.qst | 2.10 | All screens visited | 2005 of 2005 |
| q218 | The Hero Without a Name.qst | 2.10 | All screens visited | 514 of 514 |
| q219 | Triforce Peril.qst | 2.10 | All screens visited | 223 of 223 |
| q220 | IParalleleMSENG13.qst | 2.10 | All screens visited | 190 of 190 |
| q221 | Reclamation.qst | 2.10 | All screens visited | 826 of 826 |
| q222_LoZTheNineVillages | 9villages.qst | 2.10 | All screens visited | 3636 of 3636 |
| q223 | TLOZ ANL.qst | 2.10 | All screens visited | 160 of 160 |
| q224 | quests-1213208577-package-Soshis Quest New.qst | 2.10 | All screens visited | 629 of 629 |
| q226 | CQ All Grown Up.qst | 2.10 | All screens visited | 1119 of 1119 |
| q227 | childs_quest.qst | 2.10 | All screens visited | 310 of 310 |
| q228 | quests-1242237868-package-thelegendofzeldaremix.qst | 2.10 | All screens visited | 412 of 412 |
| q229 | TLoZBoaB.qst | 2.10 | All screens visited | 3926 of 3926 |
| q230 | An Ordinary Quest.qst | 2.10 | All screens visited | 502 of 502 |
| q231 | Secrets of Hyrule.qst | 2.10 | All screens visited | 388 of 388 |
| q232 | quests-1246119325-package-Heros Downfall.qst | 2.10 | All screens visited | 609 of 609 |
| q233 | preperation.qst | 2.10 | All screens visited | 385 of 385 |
| q234 | BloodlineClassic.qst | 2.10 | All screens visited | 767 of 767 |
| q235 | BA2_EB.qst | 1.90 | All screens visited | 686 of 686 |
| q236 | quests-1233540665-package-Donkey Kong.qst | 2.10 | All screens visited | 71 of 71 |
| q237_EightLands | EightLands.qst | 2.50.0 | All screens visited | 1154 of 1154 |
| q238 | LINK IN HOLBERINA.qst | 2.10 | All screens visited | 227 of 227 |
| q239 | quests-1237042229-package-The lost sequel.qst | 2.10 | All screens visited | 386 of 386 |
| q240 | SWIIDST.qst | 2.10 | All screens visited | 564 of 564 |
| q241 | JustAnotherDay.qst | 2.10 | All screens visited | 403 of 403 |
| q242_LegendofLink | Legend of Link.qst | 2.10 | All screens visited | 525 of 525 |
| q243 | StormOfChaos.qst | 2.10 | All screens visited | 367 of 367 |
| q244 | AllstarsBS.qst | 1.92 | All screens visited | 53 of 53 |
| q245 | AllstarsBSXtreme.qst | 1.92 | All screens visited | 53 of 53 |
| q246 | quests-1241744727-package-A Quest With Stuff In It.qst | 2.10 | All screens visited | 406 of 406 |
| q247 | ganonco.qst | 2.10 | All screens visited | 693 of 693 |
| q248_SecretoftheGoldenRingLegacyEdition | secretofthegoldenring.qst | 2.53.1 | All screens visited | 508 of 508 |
| q251 | OccuringsofthePast.qst | 2.10 | All screens visited | 867 of 867 |
| q252 | ExileInGrumville.qst | 2.10 | All screens visited | 1305 of 1305 |
| q254_ALinkTotheShadows | ALTTS.qst | 1.92 | All screens visited | 1068 of 1068 |
| q255 | New Beginnings.qst | 2.10 | All screens visited | 154 of 154 |
| q256 | Jons Quest.qst | 2.10 | All screens visited | 398 of 398 |
| q257_Ibuprofen | Ibuprofen.qst | 1.92 | All screens visited | 338 of 338 |
| q258 | AOQ2TOTDS.qst | 2.10 | All screens visited | 1776 of 1776 |
| q259 | CheeseNips.qst | 1.92 | All screens visited | 97 of 97 |
| q260 | Koholint_upd.qst | 2.10 | All screens visited | 722 of 722 |
| q261 | 4seasons-fix.qst | 2.10 | All screens visited | 615 of 615 |
| q263 | WOTLW.qst | 2.10 | All screens visited | 1574 of 1574 |
| q264 | Link Saves the Girl-(Update 3.0).qst | 2.10 | All screens visited | 693 of 693 |
| q265 | linksavesgirl2-fix.qst | 2.10 | All screens visited | 803 of 803 |
| q266 | NF2SH.qst | 2.10 | All screens visited | 241 of 241 |
| q266 | NF2SHALT.qst | 2.10 | All screens visited | 241 of 241 |
| q267 | LSG3.qst | 2.10 | All screens visited | 845 of 845 |
| q268_PathofDestiny | Path of Destiny.qst | 2.50.0 | All screens visited | 180 of 180 |
| q269_RiseofGanonAnniversaryEdition | Rise of Ganon.qst | 2.55 or 3.0 Nightly | All screens visited | 613 of 613 |
| q270 | The legend of Zelda, the new begining.qst | 2.10 | All screens visited | 674 of 674 |
| q271 | DarkForce94 - Ruins of the Royal Family.qst | 2.10 | All screens visited | 325 of 325 |
| q272 | SamJ Quest.qst | 2.10 | All screens visited | 187 of 187 |
| q273_TheQuestofBeings | The Quest Of Beings.qst | 2.50.0 | All screens visited | 453 of 453 |
| q274 | Docs Remix 1st.qst | 2.10 | All screens visited | 373 of 373 |
| q275 | End_of_Time_DX.qst | 2.10 | All screens visited | 834 of 834 |
| q276 | HoL.qst | 2.10 | All screens visited | 295 of 295 |
| q277 | ReturnoftheHero.qst | 2.10 | All screens visited | 1822 of 1822 |
| q278 | Aidan's adventure.qst | 1.92 | All screens visited | 37 of 37 |
| q279 | ganonco2.qst | 2.10 | All screens visited | 2139 of 2139 |
| q280 | ATD update.qst | 2.10 | All screens visited | 350 of 350 |
| q281 | Super Metroid Hooded Samus.qst | 2.10 | All screens visited | 87 of 87 |
| q282 | Super Metroid Hooded Samus Master Quest.qst | 2.10 | All screens visited | 165 of 165 |
| q283 | dinus.qst | 2.10 | All screens visited | 371 of 371 |
| q284 | Lands of Teratory.qst | 2.10 | All screens visited | 167 of 167 |
| q285 | TheLegendOfZeldaTheForgottenLand-Darkincarnation13.qst | 2.10 | All screens visited | 662 of 662 |
| q286_MetroidRemix | MetroidRemix-Kenster.qst | 2.10 | All screens visited | 214 of 214 |
| q287 | ben2.qst | 2.10 | All screens visited | 372 of 372 |
| q288 | LQ REMAKE.qst | 2.10 | All screens visited | 400 of 400 |
| q289 | Fawfuls Kingdom.qst | 2.10 | All screens visited | 636 of 636 |
| q290 | quests-1290622712-package-level.qst | 2.10 | All screens visited | 536 of 536 |
| q291_TheNeverendingFantasyAnniversaryEdition | Neverending Fantasy_Correct_Edition.qst | 2.50.1 | All screens visited | 821 of 821 |
| q292_LinkintheLostSky | quests-1304278411-package-LILS.qst | 2.10 | All screens visited | 1087 of 1087 |
| q293 | alinktopower.qst | 1.92 | All screens visited | 1134 of 1134 |
| q294_TheLegendofZeldatheGodofPower | AOTime.qst | 2.10 | All screens visited | 1201 of 1201 |
| q295 | Dawn to Twilight.qst | 2.10 | All screens visited | 1041 of 1041 |
| q296 | Docs Remix 2nd.qst | 2.10 | All screens visited | 383 of 383 |
| q297 | 1st-revamp.qst | 1.92 | All screens visited | 421 of 421 |
| q297 | GanonsClaim.qst | 1.92 | All screens visited | 417 of 417 |
| q297 | HiddenDualityXtremeDC.qst | 1.92 | All screens visited | 1896 of 1896 |
| q297 | TheFallenOne.qst | 1.92 | All screens visited | 404 of 404 |
| q297 | WolfStyle.qst | 1.92 | All screens visited | 190 of 190 |
| q298 | quests-1301355386-package-aloha.qst | 2.10 | All screens visited | 473 of 473 |
| q299 | LOZFlow of Time.qst | 2.10 | All screens visited | 1681 of 1681 |
| q300_1stQuestRedone | 1st Quest Remake.qst | 2.50.0 | All screens visited | 389 of 389 |
| q301 | Tirua.qst | 2.10 | All screens visited | 414 of 414 |
| q303 | quests-1306221054-package-TLoZ Hyrulean Quest.qst | 2.10 | All screens visited | 279 of 279 |
| q305 | dark storm quest.qst | 2.10 | All screens visited | 265 of 265 |
| q306 | The Quest Of Beings 2.qst | 2.10 | All screens visited | 583 of 583 |
| q307 | Links 1st day.qst | 2.10 | All screens visited | 611 of 611 |
| q308 | twitch.qst | 2.10 | All screens visited | 590 of 590 |
| q309 | HeroOfElements.qst | 2.10 | All screens visited | 1914 of 1914 |
| q310 | 16Caballo.qst | 1.92 | All screens visited | 100 of 100 |
| q311 | twitchl.qst | 2.10 | All screens visited | 967 of 967 |
| q312 | quests-1323307506-package-LightoftheHeavensDX.qst | 2.50 beta | All screens visited | 591 of 591 |
| q313 | quests-1322995927-package-9BOULDERSREVENGE.qst | 2.11 | All screens visited | 18 of 18 |
| q314 | treasure.qst | 2.50 beta | All screens visited | 587 of 587 |
| q315 | myfirst.qst | 2.50 beta | All screens visited | 407 of 407 |
| q316 | Requiem.qst | 2.50 beta | All screens visited | 437 of 437 |
| q317_BowlAmerica | BowlAmerica.qst | 2.50.0 | All screens visited | 99 of 99 |
| q318_IHateMayonnaise | ihatemayo.qst | 1.92 | All screens visited | 103 of 103 |
| q319_IHateMayonnaise2DepressiveDonuts | ihatemayo2.qst | 2.50.2 | All screens visited | 535 of 535 |
| q320 | Dungeon Link.qst | 2.50 beta | All screens visited | 858 of 858 |
| q321_FunintheSun8bitExtraordinare | FunintheSun.qst | 2.50.2 | All screens visited | 518 of 518 |
| q322 | quests-1324187919-package-hollacaust.qst | 2.11 | All screens visited | 76 of 76 |
| q323 | The Journey Of link.qst | 2.50 beta | All screens visited | 218 of 218 |
| q324 | marioxmas.qst | 1.92 | All screens visited | 1099 of 1099 |
| q325 | mirfull.qst | 1.92 | All screens visited | 976 of 976 |
| q326_ZeldaANewDawn | ANewDawn1.25.qst | 2.50.0 | All screens visited | 846 of 846 |
| q327 | Links 2nd Day.qst | 2.10 | All screens visited | 635 of 635 |
| q328 | Links stupid Quest.qst | 2.50 beta | All screens visited | 54 of 54 |
| q329 | EALS.qst | 2.50 beta | All screens visited | 406 of 406 |
| q330 | quests-1327100751-package-ILCIH-GA.qst | 1.92 | All screens visited | 249 of 249 |
| q331 | Somewhere In Between.qst | 2.50 beta | All screens visited | 184 of 184 |
| q333 | pathofshadows.qst | 2.50 beta | All screens visited | 658 of 658 |
| q334_EngageToZeldawock | Engage to Zeldawock.qst | 2.53.1 | All screens visited | 186 of 186 |
| q335_TheLiberationofHyrule | TheLiberationOfHyrule.qst | 2.53.0 | All screens visited | 621 of 621 |
| q336 | ZeldasButt-Spram.qst | 2.10 | All screens visited | 1837 of 1837 |
| q337 | The Random NES Quest Made in One Day - L L L L L LINK.qst | 2.10 | All screens visited | 104 of 104 |
| q338_IslandAdventure | Island_Adventure_Deutsch.qst | 2.50 beta | All screens visited | 311 of 311 |
| q338_IslandAdventure | Island_Adventure_English.qst | 2.50 beta | All screens visited | 311 of 311 |
| q339 | NES Remake.qst | 2.11 | All screens visited | 340 of 340 |
| q340 | A very confusing quest.qst | 2.11 | All screens visited | 412 of 412 |
| q341 | royaldarkness.qst | 2.50 beta | All screens visited | 498 of 498 |
| q342_HerosLegend | Hero's Legend.qst | 2.50.0 | All screens visited | 867 of 867 |
| q343_DayDay | Day Day - joelmacool12.qst | 2.50.0 | All screens visited | 74 of 74 |
| q344 | Link's 3rd Day - joelmacool12.qst | 2.10 | All screens visited | 661 of 661 |
| q345_IAmBored | I Am Bored.qst | 2.50.0 | All screens visited | 506 of 506 |
| q348_TheQuestofLamda | LBDT.qst | 2.50.0 | All screens visited | 323 of 323 |
| q349 | Thismightbegood.qst | 2.50 beta | All screens visited | 254 of 254 |
| q350_FirstQuestLayered | First Quest Layered.qst | 2.50.0 | All screens visited | 376 of 376 |
| q351 | RandomNESquest.qst | 2.10 | All screens visited | 316 of 316 |
| q352 | Reverse.qst | 2.50 beta | All screens visited | 367 of 367 |
| q353 | HoL2.qst | 2.10 | All screens visited | 343 of 343 |
| q354 | bighunk.qst | 2.50 beta | All screens visited | 466 of 466 |
| q355 | princesstale.qst | 2.50 beta | All screens visited | 422 of 422 |
| q356 | X-Flakes.qst | 2.50 beta | All screens visited | 131 of 131 |
| q357_RedmageAdamsAdventure | RedmageAdam&#39_s Adventure.qst | 2.50.0 | All screens visited | 1239 of 1239 |
| q358_Stranded | stranded.qst | 2.50.2 | All screens visited | 1239 of 1239 |
| q359_CarnageinSpace | Carnage in Space.qst | 2.50.0 | All screens visited | 166 of 166 |
| q360_LinksFourthDay | Links 4th Day.qst | 2.50.0 | All screens visited | 578 of 578 |
| q361 | resurrection.qst | 2.50.0 | All screens visited | 1657 of 1657 |
| q362 | The Light Within.qst | 2.50.0 | All screens visited | 1208 of 1208 |
| q363_TLoZFlashback | TLoZ Flashback.qst | 2.50.0 | All screens visited | 456 of 456 |
| q365 | Fight the Monsters II.qst | 2.50.0 | All screens visited | 122 of 122 |
| q366 | The Darknut Within.qst | 2.50.0 | All screens visited | 88 of 88 |
| q368_MystIsland | Thy Myst Isle.qst | 2.50.1 | All screens visited | 875 of 875 |
| q369 | tropicana.qst | 2.50 beta | All screens visited | 1079 of 1079 |
| q370_TheHerosMemory | Hero's Memory.qst | 2.50.2 | All screens visited | 915 of 915 |
| q371_Abigail | Abigail.qst | 2.50.0 | All screens visited | 495 of 495 |
| q372_LinkedTothePast | LinkedTothePast.qst | 2.50.0 | All screens visited | 1396 of 1396 |
| q373_LinksQuestfortheHookshot2Quest | Quest.qst | 2.53.1 | All screens visited | 1352 of 1352 |
| q374_TheDarknessWithin | DarknessWithin.qst | 2.50.0 | All screens visited | 1170 of 1170 |
| q375_ZeldaConquest | conquestfinalupdate.qst | 2.50.0 | All screens visited | 834 of 834 |
| q376_ALinkToTheZelda | ALinkToTheZelda.qst | 2.50.0 | All screens visited | 165 of 165 |
| q380_GolvelliuslaQueteduSecond | Golvellius.qst | 2.50 beta | All screens visited | 995 of 995 |
| q381_DawntoTwilightPlus | Dawn to Twilight Plus.qst | 2.50.1 | All screens visited | 1065 of 1065 |
| q382_SecondQuestLayered | Second Quest Layered.qst | 2.50.0 | All screens visited | 475 of 475 |
| q383_AShortQuest | ShortQuest.qst | 2.50.0 | All screens visited | 50 of 50 |
| q384_TheLegendOfAmyRoseTheAdventureBegins | Legend Of Amy Rose.qst | 2.50.0 | All screens visited | 1588 of 1588 |
| q385_Antiquity | Antiquity.qst | 2.50.1 | All screens visited | 733 of 733 |
| q386_TheTroubleWithExiledWarriors2 | tew2.qst | 2.50.0 | All screens visited | 1173 of 1173 |
| q387_LinksRise | Link's Rise_V2_0.qst | 2.55 or 3.0 Nightly | All screens visited | 622 of 622 |
| q388_RandomizerClassic | Randomizer.qst | 2.50.0 | All screens visited | 2172 of 2172 |
| q389_RandomizerModern | RandomizerModern.qst | 2.50.0 | All screens visited | 2273 of 2273 |
| q396_ShadowProvidenceupdateV12 | shadow providence.qst | 2.50 beta | All screens visited | 554 of 554 |
| q397_QuestofDarkness | Quest of Darkness.qst | 2.50.0 | All screens visited | 1231 of 1231 |
| q398_LinksErection | link&#39_s erection 2point5.qst | 2.50.0 | All screens visited | 144 of 144 |
| q399_LinkSwordWin | LinkSwordWin.qst | 2.50.0 | All screens visited | 134 of 134 |
| q400_FunintheSunIIShadowoftheFirst | ShadowOfTheFirst.qst | 2.50.2 | All screens visited | 583 of 583 |
| q401_TheTriforceofTerror | quest v1.2.qst | 2.50.0 | All screens visited | 1286 of 1286 |
| q401_TheTriforceofTerror | quest v1.3.qst | 2.50.0 | All screens visited | 1286 of 1286 |
| q402_PessimisticPizza | PessimisticPizza.qst | 2.50.0 | All screens visited | 736 of 736 |
| q403_LegacyofGanon | Legacy.qst | 2.50.0 | All screens visited | 340 of 340 |
| q404_CursorQuest | Cursor_Challenge.qst | 2.50.0 | All screens visited | 161 of 161 |
| q405_LinkvstheUSGovernment | link vs the us government.qst | 2.50.0 | All screens visited | 25 of 25 |
| q406_TheTitansQuestDirectorsCut | TQDC.qst | 2.50.0 | All screens visited | 2104 of 2104 |
| q407_PuzzleRobotsDeluxe | Puzzle_Robots_Deluxe.qst | 2.50.2 | All screens visited | 321 of 321 |
| q408_TaleranDarkRule | Taleran Dark Rule.qst | 2.50.0 | All screens visited | 1037 of 1037 |
| q409_NewQuest2013 | new2013.qst | 2.50.2 | All screens visited | 442 of 442 |
| q410_Anotsonormalzeldagame2Weeks | You Tell Me.qst.qst | 2.50.0 | All screens visited | 317 of 317 |
| q411_GreenNinja2WeekContest | GreenNinja(prototype).qst | 2.50 beta | All screens visited | 82 of 82 |
| q412_IMaQi6HWISHS2WA2Week | IMaQWISHS2WA.qst | 2.50.0 | All screens visited | 43 of 43 |
| q413_Russian2Weeks | Russian.qst | 2.50.0 | All screens visited | 98 of 98 |
| q414_VaultofSouls2weeks | VaultOfSouls.qst | 2.50.0 | All screens visited | 294 of 294 |
| q415_TheLegendofZeldaTalesofChristoph2Weeks | TalesOfChristoph.qst | 2.50.0 | All screens visited | 193 of 193 |
| q416_ABoyandHisCursor2Weeks | A_Boy_and_His_Cursor(2Weeks).qst | 2.50.0 | All screens visited | 117 of 117 |
| q416_ABoyandHisCursor2Weeks | A_Boy_and_His_Cursor(Updated).qst | 2.50.1 | All screens visited | 118 of 118 |
| q417_Anthus2WeekEntry2weeks | Anthus2Week.qst | 2.50.0 | All screens visited | 80 of 80 |
| q418_ReturnToZorkda2Weeks | rtz.qst | 2.50.0 | All screens visited | 542 of 542 |
| q419_Nostalrul2Weeks | Nostalrul.qst | 2.50.0 | All screens visited | 214 of 214 |
| q420_Xaviersepicquest2week | Xavier's.qst | 2.50.0 | All screens visited | 436 of 436 |
| q422_BlockSmasher | Block_Smasher.qst | 2.50.1 | All screens visited | 161 of 161 |
| q423_FleckQuest | fleckquest.qst | 2.50.0 | All screens visited | 198 of 198 |
| q424_Escape | Escape.qst | 2.50.0 | All screens visited | 76 of 76 |
| q425_TheTriforceofCorruption | Corruption.qst | 2.50.0 | All screens visited | 798 of 798 |
| q426_TheDeathKing | The Death King.qst | 2.50.0 | All screens visited | 648 of 648 |
| q427_Questofthe4Scrolls | Quest of the 4 Scrolls.qst | 2.50.2 | All screens visited | 769 of 769 |
| q428_Pong | Pong.qst | 2.50.1 | All screens visited | 6 of 6 |
| q429_GRIKARUGUN | GRIKARUGUN.qst | 2.50 beta | All screens visited | 41 of 41 |
| q430_HandsofAtlas | Atlas.qst | 2.50.0 | All screens visited | 521 of 521 |
| q431_The3Forces | 3FHeartless.qst | 2.50.0 | All screens visited | 769 of 769 |
| q431_The3Forces | 3Forces.qst | 2.50.0 | All screens visited | 770 of 770 |
| q432_LegendOfZeldaTemplesofTurmoil | LOZTemplesOfTurmoilXTra.qst | 2.50.0 | All screens visited | 638 of 638 |
| q434_ANewAdventure | A New Adventure.qst | 2.50.0 | All screens visited | 662 of 662 |
| q435_EddysTrollDay | Eddys Troll Day.qst | 2.50.0 | All screens visited | 519 of 519 |
| q436_MasterQuestChapter2 | master2-25pwd.qst | 2.50.1 | All screens visited | 5231 of 5231 |
| q437_MetroidConfusion | Metroid Confusion.qst | 2.50.0 | All screens visited | 331 of 331 |
| q438_JamesQuestRemastered | jamesr-2.50.2.qst | 2.50.2 | All screens visited | 1006 of 1006 |
| q439_ThelegendofZelda5thquestentrybyTravster | 5th.qst | 2.50.0 | All screens visited | 377 of 377 |
| q440_TheReal5thQuest5thweekquest | TheReal5thQuest.qst | 2.50.0 | All screens visited | 405 of 405 |
| q441_MischaPetyas5thQuest | 5thQuestMischa12.qst | 2.50.0 | All screens visited | 509 of 509 |
| q442_LoveForFires5thQuestEntry | LFF5th.qst | 2.50.1 | All screens visited | 471 of 471 |
| q443_HereWeGoAgain | Scootaloo-5th.qst | 2.50.0 | All screens visited | 493 of 493 |
| q444_ZeldaReverse1stQ | Zelda Reverse 1stQ.qst | 2.50.0 | All screens visited | 367 of 367 |
| q445_BtjCrafts5thQuestEntry | BtjCrafts5th.qst | 2.50.1 | All screens visited | 366 of 366 |
| q446_PromisedLands | Promised Lands.qst | 2.50.2 | All screens visited | 921 of 921 |
| q447_SpaceAdventure | Space_Adventure_Deutsch.qst | 2.50.1 | All screens visited | 205 of 205 |
| q447_SpaceAdventure | Space_Adventure_English.qst | 2.50.1 | All screens visited | 205 of 205 |
| q448_TheLegendOfAmyRose2VaatisWrath | Legend Of Amy Rose 2.qst | 2.50 beta | All screens visited | 1284 of 1284 |
| q449_Quest744 | Q744LE.qst | 2.53.1 | All screens visited | 1153 of 1153 |
| q450_TheRealmofCourage | TheRealmOfCourage.qst | 2.50.0 | All screens visited | 488 of 488 |
| q450_TheRealmofCourage | TheRealmOfCourageEasy.qst | 2.50.0 | All screens visited | 488 of 488 |
| q453_LinksQuestforChristmas | Links Quest for Christmas.qst | 2.50.1 | All screens visited | 72 of 72 |
| q454_DungeonsDarknuts | Dungeons&Darknuts.qst | 2.50.2 | All screens visited | 292 of 292 |
| q455_TheMadnessDungeon | Madness.qst | 2.50.0 | All screens visited | 56 of 56 |
| q456_LinksHardQuest | Spaghetti.qst | 2.50.0 | All screens visited | 115 of 115 |
| q457_TLoZFlashback2ndQuest | TLoZ Flashback 2nd.qst | 2.50.0 | All screens visited | 392 of 392 |
| q458_YetosQuest | YetoQuestUpdate.qst | 2.50.0 | All screens visited | 472 of 472 |
| q459_QuestofLight | Quest of Light.qst | 2.50.2 | All screens visited | 543 of 543 |
| q460_digtherockqst | digtherock.qst | 2.50.0 | All screens visited | 33 of 33 |
| q461_IsleofRebirth | Isle of Rebirth.qst | 2.50.0 | All screens visited | 3241 of 3241 |
| q462_CJC5thReskin | CJC5thReskin.qst | 2.50.1 | All screens visited | 587 of 587 |
| q464_TowerofShadows25 | Tower of Shadows 2.5.qst | 2.50.1 | All screens visited | 2168 of 2168 |
| q465_SaviorofSouls | SaviorOfSouls.qst | 2.50.0 | All screens visited | 1749 of 1749 |
| q466_LinksBadTrip | Links Bad Trip.qst | 2.50.0 | All screens visited | 27 of 27 |
| q467_LostIdentity | lostidentity2015(update).qst | 2.50.1 | All screens visited | 991 of 991 |
| q468_TheLegendofZelda | The Legend of Zelda.qst | 2.50 beta | All screens visited | 367 of 367 |
| q469_LinksQuestfortheHookshot | Link&#39_sQuestForTheHookshot.qst | 2.50.0 | All screens visited | 1215 of 1215 |
| q470_PhoenixWrightBakingTycoon | Pheonix Wright Baking Tycoon.qst | 2.50.0 | All screens visited | 138 of 138 |
| q471_LinksLastLeg | linkslastleg.qst | 2.50.1 | All screens visited | 540 of 540 |
| q472_MemorialQuest | Glenn_Memorial_Quest.qst | 2.50.1 | All screens visited | 530 of 530 |
| q473_Myrule | Myrule.qst | 2.50.2 | All screens visited | 246 of 246 |
| q474_Bobsquest | bob.qst | 2.50.0 | All screens visited | 415 of 415 |
| q475_Thequestwithoutaname | The quest without a name.qst | 2.50.0 | All screens visited | 101 of 101 |
| q476_MazeofHyrule | Maze of hyrule.qst | 2.50.0 | All screens visited | 280 of 280 |
| q477_AShortandSimplePlatformer | A Short and Simple Platformer.qst | 2.50.2 | All screens visited | 135 of 135 |
| q479_FighttheMonsters | Fight the Monsters.qst | 2.50.1 | All screens visited | 138 of 138 |
| q480_LinksFirstJourney | Link's First Journey.qst | 2.50.2 | All screens visited | 454 of 454 |
| q481_ASurvivalQuest | A Survival Quest.qst | 2.50.0 | All screens visited | 23 of 23 |
| q482_QuestofLamdaII | Quest Of Lamda II.qst | 2.50.0 | All screens visited | 336 of 336 |
| q483_Throwback | Throwback.qst | 2.50.0 | All screens visited | 2988 of 2988 |
| q485_HerosStory | HS_Hero.qst | 2.50.0 | All screens visited | 435 of 435 |
| q485_HerosStory | HS_Normal.qst | 2.50.0 | All screens visited | 435 of 435 |
| q486_Eiyuu | Eiyuu.qst | 2.50.0 | All screens visited | 610 of 610 |
| q487_BigJoes5thQuestEntrySpecialEdition | bigjoe_5th.qst | 2.50.1 | All screens visited | 562 of 562 |
| q488_RandomizerSuperVersion | RandomizerSuper.qst | 2.50.0 | All screens visited | 2697 of 2697 |
| q489_MysteriousWorld | MysteriousWorld.qst | 2.50.0 | All screens visited | 238 of 238 |
| q490_LegendofZeldaOracleofAwesomeness | Oracle of Awesomeness.qst | 2.50.0 | All screens visited | 901 of 901 |
| q491_LegendHasIt | Legend Has It.qst | 2.50.0 | All screens visited | 797 of 797 |
| q492_HeroofTime | Hot2.5.qst | 2.50.0 | All screens visited | 715 of 715 |
| q493_ZeldaFlowofTimeRemasteredDAWNVERSION | LOZ_FlowRemastered.qst | 2.53.1 | All screens visited | 2379 of 2379 |
| q494_Hitodama | HitodamaDX.qst | 2.50.2 | All screens visited | 2296 of 2296 |
| q495_OneMoreJourney | OneMoreJourney.qst | 2.50.0 | All screens visited | 2227 of 2227 |
| q496_MazeofHyrule2 | Maze of Hyrule 2.qst | 2.53.1 | All screens visited | 783 of 783 |
| q497_LinkstuckinCastlevania | LsiCV117.qst | 2.53.0 | All screens visited | 458 of 458 |
| q498_Wellthatssomething | Well that's something.qst | 2.50.1 | All screens visited | 24 of 24 |
| q499_AdvenutreofMarinaqst | adventure of marina.qst | 2.50.0 | All screens visited | 594 of 594 |
| q500_Nightmares5thQuestFinalVersion | 5thnightmare-final.qst | 2.50.1 | All screens visited | 530 of 530 |
| q501_TheLegendofZeldaGanonsLegacy | legacy.qst | 2.50.1 | All screens visited | 900 of 900 |
| q502_LinksAwakeningZeldaClassicEdition | laclassic.qst | 2.50.1 | All screens visited | 729 of 729 |
| q503_LinktothePastClassic | lttpclassic.qst | 2.50.1 | All screens visited | 1791 of 1791 |
| q504_SuperMetroidZeldaClassicEdition | smclassic.qst | 2.50.1 | All screens visited | 1685 of 1685 |
| q505_TheIslandsofZelda | ZIslands.qst | 2.10 | All screens visited | 2386 of 2386 |
| q506_LegendoftheSixLegendaryGoldenGreenCacti | LotSLGGC.qst | 2.50.0 | All screens visited | 315 of 315 |
| q507_HyleanRevolutionEpisode1GanonsAssault | Hylean Revolution Episode 1 - Ganons Assault.qst | 2.50.0 | All screens visited | 877 of 877 |
| q508_TheLegendofZeldaTowerofCourage | Tower of Courage.qst | 2.50.1 | All screens visited | 1698 of 1698 |
| q509_MegaManLinkEdition | Link Man.qst | 2.50.1 | All screens visited | 252 of 252 |
| q510_FedoraToss | Fedora Toss.qst | 2.50.0 | All screens visited | 96 of 96 |
| q511_ShamanockAdvance | Shamanock-Advance.qst | 2.50.0 | All screens visited | 1008 of 1008 |
| q512_LikeLikeZm23 | LikeLikeBX8.qst | 2.50.0 | All screens visited | 307 of 307 |
| q513_RandomizerBSZeldaVersion | RandomizerBSZelda.qst | 2.50.0 | All screens visited | 2293 of 2293 |
| q514_Wellthatsalsosomething | Well that's also something.qst | 2.50.1 | All screens visited | 52 of 52 |
| q515_RoyaldarknessRemastered | LoZ-RoyaldarknessRemastered.qst | 2.50.2 | All screens visited | 898 of 898 |
| q515_RoyaldarknessRemastered | LoZ-RoyaldarknessRemasteredHeroMode.qst | 2.50.2 | All screens visited | 898 of 898 |
| q516_SpiritForest | Spirit Forest.qst | 2.50.1 | All screens visited | 106 of 106 |
| q517_Bored | Bored.qst | 2.50.0 | All screens visited | 16 of 16 |
| q518_Border | Border.qst | 2.50.0 | All screens visited | 9 of 9 |
| q519_LegendofZeldaIslandsofDeath | NEW2.qst | 1.92 | All screens visited | 339 of 339 |
| q520_LegendofZeldaCastlesofPeril | COPFixedV.qst | 2.50.0 | All screens visited | 501 of 501 |
| q521_SCKnucklesWorstNightmare | SCKnuckles' Worst Nightmare.qst | 2.50.0 | All screens visited | 604 of 604 |
| q522_Bordest | Bordest.qst | 2.50.0 | All screens visited | 196 of 196 |
| q523_TheLegendofZeldaPathofCourage | TLoZ - Path of Courage.qst | 2.50.0 | All screens visited | 47 of 47 |
| q524_ToTheTop | ToTheTop.qst | 2.10 | All screens visited | 3055 of 3055 |
| q525_LightoftheHeavens30 | LotH dydx.qst | 2.50.0 | All screens visited | 777 of 777 |
| q526_LinksDayOffv12HeroMode | Links Day Off Hero Mode.qst | 2.50.0 | All screens visited | 80 of 80 |
| q526_LinksDayOffv12HeroMode | Links Day Off v12.qst | 2.50.0 | All screens visited | 80 of 80 |
| q527_CykoQuest | Cyko Quest.qst | 2.50.0 | All screens visited | 539 of 539 |
| q528_InstrumentsofCourage | IOC.qst | 2.50.0 | All screens visited | 1576 of 1576 |
| q529_FudgesQuest | Fudge's Quest.qst | 2.50.0 | All screens visited | 360 of 360 |
| q529_FudgesQuest | quest.qst | unknown | Refused: not a recognised quest file |  |
| q530_LyricsofDeath | LoD.qst | 2.50.0 | All screens visited | 238 of 238 |
| q531_ASurvivalQuest2 | A Survival Quest 2.qst | 2.50.0 | All screens visited | 41 of 41 |
| q532_Pferd | Pferd.qst | 1.92 | All screens visited | 119 of 119 |
| q533_Cheval | Cheval.qst | 2.50.1 | All screens visited | 178 of 178 |
| q534_JMCsTrollDay | annoyingquest.qst | 2.50.1 | All screens visited | 12 of 12 |
| q535_EverybodysTrollDay | Everybody's troll day.qst | 2.50.0 | All screens visited | 41 of 41 |
| q536_Mariosshortquest | Mario's short quest.qst | 2.53.1 | All screens visited | 285 of 285 |
| q537_TheForbiddenCity | The Forbidden City.qst | 2.50.2 | All screens visited | 1425 of 1425 |
| q538_ZeldasRevelation | ZelRev.qst | 2.50.0 | All screens visited | 700 of 700 |
| q539_DayDayHD | DayDayHD.qst | 2.50.0 | All screens visited | 75 of 75 |
| q540_BalanceofNature | balance255.qst | 2.55 or 3.0 Nightly | All screens visited | 2460 of 2460 |
| q541_MoblinInvasion | MoblinInvasion.qst | 2.50.0 | All screens visited | 300 of 300 |
| q542_Isolation | Isolation.qst | 2.50.0 | All screens visited | 129 of 129 |
| q543_MooshsTrollGame | Moosh's Troll Game.qst | 2.50.0 | All screens visited | 369 of 369 |
| q544_TheNES192Quest | nes192quest.qst | 1.92 | All screens visited | 288 of 288 |
| q545_TheGreatestRescue | TheGreatestRescue-Release.qst | 2.50.0 | All screens visited | 217 of 217 |
| q547_LegendofLana | Legend of Lana.qst | 2.50.0 | All screens visited | 799 of 799 |
| q548_Iammadday | I am mad day.qst | 2.50.0 | All screens visited | 101 of 101 |
| q549_NewQuest22015 | new22015.qst | 2.50.2 | All screens visited | 1657 of 1657 |
| q550_DemoQuest | demo25.qst | 2.50.1 | All screens visited | 346 of 346 |
| q551_LinksAdventureinBlockLand | Link's Adventure in Block Land.qst | 2.53.1 | All screens visited | 287 of 287 |
| q552_HowDoIPlayZelda | How Do I Play Zelda.qst | 2.50.0 | All screens visited | 742 of 742 |
| q553_MazeofHyrulefail | Maze of Hyrule fail.qst | 2.50.2 | All screens visited | 509 of 509 |
| q554_RandomizerOutlands | RandomizerOutlands.qst | 2.50.2 | All screens visited | 3080 of 3080 |
| q555_BSZelda250 | BS_Zelda.qst | 2.50.0 | All screens visited | 320 of 320 |
| q556_MargaMenIIMuushEdish | Marga Men.qst | 2.50.0 | All screens visited | 369 of 369 |
| q557_JMCsTrollDay2 | JMC Troll Day 2.qst | 2.50.0 | All screens visited | 19 of 19 |
| q558_Iammadday2 | I am mad day 2.qst | 2.50.1 | All screens visited | 260 of 260 |
| q559_TheLandofAnarchy | 2_quest.qst | unknown | Refused: not a recognised quest file |  |
| q559_TheLandofAnarchy | quest.qst | unknown | Refused: not a recognised quest file |  |
| q559_TheLandofAnarchy | The Land of Anarchy Ganon.qst | 2.53.1 | All screens visited | 2012 of 2012 |
| q559_TheLandofAnarchy | The Land of Anarchy Zelda.qst | 2.53.1 | All screens visited | 2020 of 2020 |
| q559_TheLandofAnarchy | The Land of Anarchy.qst | 2.53.1 | All screens visited | 2086 of 2086 |
| q560_LINKRank9 | Rank 9.qst | 2.50.3 | All screens visited | 1152 of 1152 |
| q561_TheOneWeekLateAdventure | Adventure.qst | 2.50.1 | All screens visited | 235 of 235 |
| q562_Zelda3TheReturnofGanon | Zelda3.qst | 2.50.1 | All screens visited | 967 of 967 |
| q563_ReturntoKoholint | Return to Koholint.qst | 2.50.1 | All screens visited | 637 of 637 |
| q564_The5thSword | 5thsword.qst | 2.50.1 | All screens visited | 529 of 529 |
| q565_TheLegendofZeldaLinksPeril | UnityAlexTLOZ_Links-Peril.qst | 2.50.1 | All screens visited | 222 of 222 |
| q566_TheThirdLegacy | The Third Legacy v1.2.qst | 2.53.1 | All screens visited | 1161 of 1161 |
| q567_DawnofANewDay | DawnofANewDay.qst | 2.50.2 | All screens visited | 452 of 452 |
| q568_IntheLostKingdomoftheBananaBloodGod | ItLKotBBGFinal.qst | 2.50.2 | All screens visited | 532 of 532 |
| q569_PassageThroughTime | passage255.qst | 2.55 or 3.0 Nightly | All screens visited | 2796 of 2796 |
| q570_SummoningWar | TheSummoningWar.qst | 2.50.1 | All screens visited | 382 of 382 |
| q571_ANewYearsExperiment | choose.qst | 2.50.1 | All screens visited | 95 of 95 |
| q572_LegendofAvataro | Legend of Avataro Final v1.1.qst | 2.50.2 | All screens visited | 68 of 68 |
| q573_RandomizerAlpha | RandomizerAlpha.qst | 2.50.0 | All screens visited | 5066 of 5066 |
| q574_DungeonImpossibleDX | DungeonImpossible.qst | 2.50.2 | All screens visited | 559 of 559 |
| q582_ForbiddenAscent | ForbiddenAscent.qst | 2.50.2 | All screens visited | 1095 of 1095 |
| q583_NightNightDX | NightNight DX.qst | 2.50.2 | All screens visited | 423 of 423 |
| q584_MatthewBluefoxQuestPack | 1stHM.qst | 2.53.1 | All screens visited | 530 of 530 |
| q584_MatthewBluefoxQuestPack | 1stMini.qst | 2.53.1 | All screens visited | 318 of 318 |
| q584_MatthewBluefoxQuestPack | 2ndHM.qst | 2.53.1 | All screens visited | 528 of 528 |
| q584_MatthewBluefoxQuestPack | 2ndMini.qst | 2.53.1 | All screens visited | 330 of 330 |
| q584_MatthewBluefoxQuestPack | 3rdHM.qst | 2.53.1 | All screens visited | 599 of 599 |
| q584_MatthewBluefoxQuestPack | LOZRandyMaxi.qst | 2.53.1 | All screens visited | 1813 of 1813 |
| q585_TaleofLyria | ToL.qst | 2.50.2 | All screens visited | 459 of 459 |
| q586_ASimpleQuest | Simple Quest.qst | 2.50.1 | All screens visited | 574 of 574 |
| q587_ImThinkingSwords | ITS.qst | 2.50.0 | All screens visited | 34 of 34 |
| q588_SteveamentusUniverse | SteveamentusUniverse.qst | 2.50.0 | All screens visited | 65 of 65 |
| q591_TLoZ25ClassicKoten13 | 1stClassic.qst | 2.50.2 | All screens visited | 367 of 367 |
| q591_TLoZ25ClassicKoten13 | 1stKoten.qst | 2.50.2 | All screens visited | 367 of 367 |
| q591_TLoZ25ClassicKoten13 | 2ndClassic.qst | 2.50.2 | All screens visited | 355 of 355 |
| q591_TLoZ25ClassicKoten13 | 2ndKoten.qst | 2.50.2 | All screens visited | 355 of 355 |
| q592_UmbralCloud | UmbralCloud.qst | 2.50.1 | All screens visited | 307 of 307 |
| q593_BeyondHyrule | Beyond Hyrule V1.qst | 2.50.1 | All screens visited | 495 of 495 |
| q594_TheLegendaryArrows | The Legendary Arrows.qst | 2.50.0 | All screens visited | 433 of 433 |
| q595_linkslegendspt1 | link's legends part 1.qst | 2.50.1 | All screens visited | 535 of 535 |
| q596_FightforFreedom | Fight for Freedom.qst | 2.50.1 | All screens visited | 24 of 24 |
| q597_ATributetoYetosQuest | ATributeToYeto'sQuest.qst | 2.50.0 | All screens visited | 252 of 252 |
| q598_LegendofLanaDXDirectorsCut | Legend of Lana DX.qst | 2.50.2 | All screens visited | 1516 of 1516 |
| q599_BrockLesnarqst | Brock Lesnar.qst | 2.50.2 | All screens visited | 88 of 88 |
| q600_4Temples | 4 Temples.qst | 2.50.0 | All screens visited | 128 of 128 |
| q601_TheDarkWorldsReturn | TDWR1_1.qst | 2.50.2 | All screens visited | 539 of 539 |
| q602_TheIslandofYeto | TheIslandOfYeto.qst | 2.50.0 | All screens visited | 656 of 656 |
| q603_MetroidOrigins | origins.qst | 2.53.1 | All screens visited | 1319 of 1319 |
| q604_4Temples2FateofTheTriforce | 4T2.qst | 2.50.1 | All screens visited | 289 of 289 |
| q605_NargadsTrailCrystalCrusades | NTCC.qst | 2.50.2 | All screens visited | 2177 of 2177 |
| q606_Nehe | nehe.qst | 2.50.2 | All screens visited | 104 of 104 |
| q607_RetreattoCrumsc | Crumsc.qst | 2.50.0 | All screens visited | 393 of 393 |
| q608_EntertheGoldenAquamentus | Enter the Golden Aquamentus.qst | 2.50.2 | All screens visited | 28 of 28 |
| q609_MagicandFantasy | M&F20zc.qst | 2.50.2 | All screens visited | 727 of 727 |
| q610_TurtlesHateCandy | TurtlesHateCandy.qst | 2.50.2 | All screens visited | 694 of 694 |
| q611_RandomizerReturns | RandomizerReturns.qst | 2.50.0 | Partly visited (time limit) | 4595 of 7314 |
| q612_LinksGenericQuest | thing.qst | 2.50.1 | All screens visited | 223 of 223 |
| q613_theDarkTreasureVersion21 | The Legend Of Zelda The Dark Treasure EN.qst | 2.50.1 | All screens visited | 1235 of 1235 |
| q613_theDarkTreasureVersion21 | The Legend Of Zelda The Dark Treasure FR.qst | 2.50.1 | All screens visited | 1235 of 1235 |
| q614_Quest447 | Q447.qst | 2.50.2 | All screens visited | 398 of 398 |
| q616_Yuurei | YuureiDX.qst | 2.50.2 | All screens visited | 1869 of 1869 |
| q617_ZeldaNESRemastered | zeldanesremastered.qst | 2.50.2 | All screens visited | 1545 of 1545 |
| q618_LinkandZeldaPanoplyofCalatia | Link and Zelda - Panoply of Calatia.qst | 2.50.2 | Partly visited (time limit) | 2371 of 2672 |
| q619_Zelda1BossRush | Zelda1BossRush.qst | 2.50.2 | All screens visited | 67 of 67 |
| q620_AvatarosContestQuestMix | Picross Made in a Day.qst | 2.50.2 | All screens visited | 29 of 29 |
| q620_AvatarosContestQuestMix | Pixellated Architecture Dungeon.qst | 2.50.1 | All screens visited | 62 of 62 |
| q620_AvatarosContestQuestMix | Story Of 24 Hours.qst | 2.50.2 | All screens visited | 29 of 29 |
| q620_AvatarosContestQuestMix | Thinking without Logic.qst | 2.50.1 | All screens visited | 24 of 24 |
| q621_LegendofPokemonWarrior | LegendOfPokemonWarrior.qst | 2.50.2 | All screens visited | 2813 of 2813 |
| q622_Zelda1BossRushRemix | Zelda1BossRushRemix.qst | 2.50.2 | All screens visited | 381 of 381 |
| q623_RiteoftheStorm | Rite of the Storm.qst | 2.50.2 | All screens visited | 256 of 256 |
| q624_AnotherLand | AnotherLand.qst | 2.50.1 | All screens visited | 272 of 272 |
| q625_TheLegendofZeldaMayathIsland | Mayath Isle.qst | 2.50.1 | All screens visited | 1331 of 1331 |
| q626_YuureiRandomizer | YuureiRandomizer.qst | 2.50.2 | All screens visited | 789 of 789 |
| q627_Zeltris | Zeltris.qst | 2.50.2 | All screens visited | 59 of 59 |
| q628_PrismaticTriangle | Pris.Triangle.qst | 2.50.2 | All screens visited | 644 of 644 |
| q629_IcyZeldaqst | IcyZelda.qst | 2.50.2 | All screens visited | 551 of 551 |
| q630_ElementalJourney | ElementalJourney.qst | 2.50.1 | All screens visited | 803 of 803 |
| q631_ChocolateShadowTemple | Chocolate Shadow Temple.qst | 2.50.0 | All screens visited | 138 of 138 |
| q632_HyruleRemastered | Hyrule Remastered.qst | 2.50.2 | All screens visited | 903 of 903 |
| q633_WhatYouDeserveA36HourQuest | What You Deserve v1.2.qst | 2.53.1 | All screens visited | 207 of 207 |
| q634_TripleTri | TripleTri.qst | 2.50.1 | All screens visited | 500 of 500 |
| q635_FASTER | FASTER1_0.qst | 2.50.2 | All screens visited | 51 of 51 |
| q636_BitsAndBites | Bits And Bites.qst | 2.50.2 | All screens visited | 421 of 421 |
| q637_NewQuestRebuilt | newquestrebuilt.qst | 2.50.2 | All screens visited | 1331 of 1331 |
| q638_4Dungeons | New Quest.qst | 2.50.1 | All screens visited | 220 of 220 |
| q639_ANewDawnTimAllenEdition | ANewDawn1.25_Tim_Allen_Edition.qst | 2.50.2 | All screens visited | 846 of 846 |
| q640_TabulaRasa | Tabula Rasa.qst | 2.50.2 | All screens visited | 151 of 151 |
| q642_RainbowLand | Rainbow Land.qst | 2.50.2 | All screens visited | 174 of 174 |
| q643_TheLegendofZeldaBeginnings | TLoZ Beginnings.qst | 2.53.1 | All screens visited | 417 of 417 |
| q644_GanonsRevengeIIDestinyofaHero | GRII.qst | 2.10 | All screens visited | 809 of 809 |
| q645_FragmentsofOld | Fragments_of_Old.qst | 2.50.2 | All screens visited | 332 of 332 |
| q646_StarshooterSupreme | Starshooter Supreme 250.qst | 2.50.3 | All screens visited | 5 of 5 |
| q647_IcyZelda2ndqst | IcyZelda2nd.qst | 2.50.2 | All screens visited | 820 of 820 |
| q648_LinkedSeasons | Linked-Seasons1.3.qst | 2.50.2 | All screens visited | 423 of 423 |
| q649_TheGlorySquad | GlorySquad.qst | 2.50.3 | All screens visited | 3790 of 3790 |
| q650_MelvintheMoblin | MtM1.0.qst | 2.50.2 | All screens visited | 1768 of 1768 |
| q651_MirageIsland | MirageIsland.qst | 2.50.2 | Partly visited (time limit) | 4118 of 4308 |
| q652_ZeldaSilentWorld | Silent World. (2).qst | 2.50.2 | All screens visited | 1415 of 1415 |
| q653_LinksGrandAdventure2 | Link's Grand Adventure 2.qst | 2.50.2 | All screens visited | 386 of 386 |
| q654_AnotherLand2MiragoIsland | AL2.qst | 2.50.1 | All screens visited | 473 of 473 |
| q655_LinksGrandAdventure3 | Link's Grand Adventure 3.qst | 2.50.2 | All screens visited | 416 of 416 |
| q656_TheLiberationofHyruleInsanityUnchained | InsanityUnchained.qst | 2.53.0 | All screens visited | 495 of 495 |
| q657_BlockMadness | blockmadness.qst | 2.50.2 | All screens visited | 58 of 58 |
| q658_HeraldofHeroes | Herald of Heroes.qst | 2.53.1 | All screens visited | 4059 of 4059 |
| q659_ZScriptHavoc | ZScript Havoc.qst | 2.50.2 | All screens visited | 300 of 300 |
| q660_Avoidtheenemiesnumber1 | Avoid the enemies number 1_.qst | 2.50.2 | All screens visited | 47 of 47 |
| q661_Theawesomestquestever | The awesomest quest ever.qst | 2.50.1 | All screens visited | 9 of 9 |
| q663_DayDayNine | TheUltraFunDayDayDeluxeExperience.qst | 2.50.2 | All screens visited | 17 of 17 |
| q664_TheLegendofAmyRose3OracleoftheEmerald | Legend of Amy Rose 3.qst | 2.50.2 | All screens visited | 1499 of 1499 |
| q665_DemoQuestSP | demosp253.qst | 2.53.0 | All screens visited | 651 of 651 |
| q666_RetreattoCrumsc2EarlyArrival | Crumsc_2.qst | 2.50.0 | All screens visited | 1024 of 1024 |
| q667_YuurandTalesoftheLabyrinth | Yuurand.qst | 2.53.1 | Partly visited (time limit) | 1460 of 4876 |
| q668_SouthbySouthrule | South by Southrule.qst | 2.53.1 | All screens visited | 464 of 464 |
| q669_RedRabbit | RedRabbit.qst | 2.50.2 | All screens visited | 205 of 205 |
| q670_Wellthatssomethingelse | Well that's something else.qst | 2.50.2 | All screens visited | 58 of 58 |
| q671_Stars | Stars.qst | 2.50.2 | All screens visited | 223 of 223 |
| q672_PrometheusEternalHero | Prometheus-Eternal-Hero253.qst | 2.53.0 | All screens visited | 1003 of 1003 |
| q673_TheLegendofSirknight | LegendofSirKnight.qst | 2.50.2 | All screens visited | 149 of 149 |
| q674_SecondChance | Second Chance.qst | 2.50.2 | All screens visited | 654 of 654 |
| q676_BalladofaBloodlineDeluxe | BBDeluxe.qst | 2.50.2 | All screens visited | 4119 of 4119 |
| q677_ASimpleQuest | SimpleQuest.qst | 2.50.2 | All screens visited | 359 of 359 |
| q678_LyricsofDeathDX | LoDDX.qst | 2.53.0 | All screens visited | 618 of 618 |
| q679_TheAdventureofKainthereturnofGanon | AofKainZC2019a.qst | 2.50.3 | All screens visited | 1146 of 1146 |
| q680_LinksRise2TheReUhReRise | LinksRise2.qst | 2.53.1 | All screens visited | 616 of 616 |
| q681_Zelda1VoiceActed | 1st.qst | 2.50.3 | All screens visited | 368 of 368 |
| q682_LinksRiseDX | LRDX1.1.qst | 2.50.2 | All screens visited | 455 of 455 |
| q683_LinksGrandAdventureTheLostGame | Link's Grand Adventure.qst | 2.50.2 | All screens visited | 423 of 423 |
| q684_TheNecklaceofAdrian | The Necklace of Adrian.qst | 2.50.2 | All screens visited | 191 of 191 |
| q685_BoardGmae | Board Gmae.qst | 2.50.2 | All screens visited | 412 of 412 |
| q686_TheLegendofZeldaBrotherhood | brotherhood.qst | 2.53.0 | All screens visited | 350 of 350 |
| q687_TrovesofTreasure | Troves of Treasure.qst | 2.55 or 3.0 Nightly | All screens visited | 346 of 346 |
| q689_LinkstuckinExperimentalArsenal | stdWeaponsDemo.qst | 2.53.1 | All screens visited | 38 of 38 |
| q690_DarkFairysCurse | Dark Fairys Curse.qst | 2.50.0 | All screens visited | 340 of 340 |
| q692_YetosHideAndSeek | YetoHideAndSeek.qst | 2.50.0 | All screens visited | 325 of 325 |
| q693_TheLegendofZeldaSealedPower | sealed power-r120.qst | 2.53.1 | All screens visited | 449 of 449 |
| q694_RandomizerOmega | RandomizerOmega.qst | 2.50.2 | Partly visited (time limit) | 3878 of 10457 |
| q695_LegendofZeldaForgottenHero | ZeldaForgottenHero.qst | 2.50.2 | All screens visited | 665 of 665 |
| q696_MikesFunHouse | Mike's Fun House.qst | 2.50.2 | All screens visited | 5064 of 5064 |
| q698_WizzrobesBigDayOut | wizzrobesbdoupdate4.qst | 2.53.1 | All screens visited | 1379 of 1379 |
| q699_TheSlipstream | The Slipstream.qst | 2.53.1 | All screens visited | 549 of 549 |
| q700_TerrorofNecromancyMini | ToN_Mini.qst | 2.53.1 | All screens visited | 320 of 320 |
| q701_TriforceKnight | Triforce_Knight_1_05.qst | 2.53.1 | All screens visited | 1494 of 1494 |
| q702_LinksAwfulAdventure | AwfulAdv.qst | 2.53.1 | All screens visited | 95 of 95 |
| q703_Darkside66 | Darkside66LOCKED.qst | 2.53.1 | All screens visited | 619 of 619 |
| q704_HyruleFantasyGB | tlozgb2.qst | 2.50.2 | All screens visited | 374 of 374 |
| q704_HyruleFantasyGB | tlozgb.qst | 2.50.2 | All screens visited | 374 of 374 |
| q705_ALinktotheWest | A Link to the West.qst | 2.55 or 3.0 Nightly | All screens visited | 464 of 464 |
| q706_OverHead | OverHead.qst | 2.53.1 | All screens visited | 192 of 192 |
| q707_TheDepthsofMalice | DepthsOfMalice.qst | 2.53.1 | All screens visited | 788 of 788 |
| q708_Nyroxland | nyroxland1.qst | 2.53.1 | All screens visited | 506 of 506 |
| q709_Metroid3ZCRemake | metroid3.qst | 2.50.2 | Partly visited (time limit) | 2235 of 2257 |
| q710_6thQuestEntryThroughtheInverseGlass | 6th Mitsukara Entry - with Rando - Version v5-7.qst | 2.53.1 | All screens visited | 816 of 816 |
| q711_GiggleTheAdventure | GiggleTheAdventure.qst | 2.50.1 | All screens visited | 199 of 199 |
| q712_TickTock | TickTock.qst | 2.50.2 | All screens visited | 118 of 118 |
| q713_Nestastic | Nestastic 1.1.qst | 2.50.2 | All screens visited | 362 of 362 |
| q713_Nestastic | Nestastic.qst | 2.50.2 | All screens visited | 447 of 447 |
| q714_RuinsofMyths | Ruins of Myths v1.1.qst | 2.50.2 | All screens visited | 319 of 319 |
| q715_LegendofLanaGaiden | Lana Gaiden.qst | 2.50.2 | All screens visited | 647 of 647 |
| q716_Reikon | Reikon.qst | 2.50.2 | All screens visited | 686 of 686 |
| q717_TheLegendofNayruRalphsAdventure | The Legend of Nayru.qst | 2.53.1 | All screens visited | 978 of 978 |
| q718_IWillAttendTheFuneral | I Will Attend The Funeral.qst | 2.53.1 | All screens visited | 251 of 251 |
| q719_sweuqaland | sweuqaland.qst | 2.53.1 | All screens visited | 199 of 199 |
| q720_Zeldavania | zeldavania.qst | 2.53.0 | All screens visited | 211 of 211 |
| q721_Darkside66Cyclebreaker | Cyclebreaker.qst | 2.53.1 | All screens visited | 1046 of 1046 |
| q722_ZeldaMayathIslandRemastered | Mayath Island Remastered.qst | 2.53.1 | All screens visited | 3065 of 3065 |
| q723_TheLegendOfZeldaQst1z3Scrolling | LegendofZelda_qst1.qst | 2.55 or 3.0 Nightly | All screens visited | 261 of 261 |
| q723_TheLegendOfZeldaQst1z3Scrolling | LegendofZelda_qst2.qst | 2.55 or 3.0 Nightly | All screens visited | 249 of 249 |
| q724_MedleyoftheGrassBow | Medley.qst | 2.50.3 | All screens visited | 888 of 888 |
| q725_ThelegendofZeldaTitansgate | TitansGateV1.1.qst | 2.50.2 | All screens visited | 34 of 34 |
| q726_TheLegendofSirknightDefinitiveEdition | sirnightFINAL4real.qst | 2.55 or 3.0 Nightly | All screens visited | 260 of 260 |
| q727_FragmentsofPowerReimagined | Fragments of Power Reimagining.qst | 2.55 or 3.0 Nightly | All screens visited | 606 of 606 |
| q728_TheBoundsofWaterford | Waterford.qst | 2.55 or 3.0 Nightly | All screens visited | 367 of 367 |
| q729_IcyZelda3rdqst | IcyZelda3rd.qst | 2.53.1 | All screens visited | 733 of 733 |
| q730_TheTriCompass | LoZTTC.qst | 2.50.2 | All screens visited | 729 of 729 |
| q731_GoGollabTheConflictionsofMorality | GoGollab_1_FunnieresterEdition.qst | 2.53.1 | All screens visited | 1361 of 1361 |
| q732_FragmentsofOld2 | Fragments_of_Old_2.qst | 2.53.1 | All screens visited | 364 of 364 |
| q733_BreathoftheWild3 | BreathOfTheWild3.qst | 2.55 or 3.0 Nightly | All screens visited | 36 of 36 |
| q734_ProjectStorm | Project Storm Beta release.qst | 2.50.2 | All screens visited | 1603 of 1603 |
| q735_DestinyoftheOracles | destiny255.qst | 2.55 or 3.0 Nightly | All screens visited | 2851 of 2851 |
| q736_AlarmClockZScriptRealTimeClockExample | eggtimer_1.0.qst | 2.55 or 3.0 Nightly | Runs; no screens to visit | 0 of 0 |
| q737_Fortressvania | Fortressvania.qst | 2.53.1 | All screens visited | 1599 of 1599 |
| q738_1stquestremakewithBSprototypegraphics | VELbsproto1st.qst | 2.50.2 | All screens visited | 387 of 387 |
| q739_MemoriesofYesteryear | Memories of Yesteryear.qst | 2.50.2 | Partly visited (time limit) | 582 of 1120 |
| q740_6thQuest | 6thQuest v121.qst | 2.50.0 | All screens visited | 2221 of 2221 |
| q741_TheLegendofZeldaHeirtotheHero | Heir_to_the_Hero.qst | 2.53.1 | All screens visited | 495 of 495 |
| q742_PaganInvaders | PaganInvaders_Gamma_2.qst | 2.55 or 3.0 Nightly | All screens visited | 1 of 1 |
| q743_NewQuest | New_v1.2.qst | 2.50.2 | All screens visited | 600 of 600 |
| q744_ArmageddonQuestForeverVer253 | ArmageddonForever2.53.qst | 2.53.1 | All screens visited | 543 of 543 |
| q745_ArmageddonQuestVer253 | Armageddon.qst | 2.53.0 | All screens visited | 515 of 515 |
| q746_Roeblozs6thQuestRWAPart2 | 6th.qst | 2.53.1 | All screens visited | 509 of 509 |
| q747_HyrulesAwakening | HyrulesAwakening.qst | 2.53.1 | All screens visited | 505 of 505 |
| q748_DragonDream | DragonDream.qst | 2.55 or 3.0 Nightly | All screens visited | 812 of 812 |
| q749_NayrusOdysseyDefinitiveEdition | NayrusOdysseyDE2.qst | 2.55 or 3.0 Nightly | All screens visited | 1089 of 1089 |
| q749_NayrusOdysseyDefinitiveEdition | NayrusOdysseyDE_fixed.qst | 2.53.1 | All screens visited | 624 of 624 |
| q750_100RoomsofWisdom | 100roomsWisdom.qst | 2.53.1 | All screens visited | 162 of 162 |
| q751_LegendofLinkTheNewLegacy | LoL New Legacy.qst | 2.53.1 | All screens visited | 1723 of 1723 |
| q752_WalkingForever | Walking Forever.qst | 2.53.1 | All screens visited | 84 of 84 |
| q753_WalkingForever2 | Walking Forever 2.qst | 2.53.1 | All screens visited | 1108 of 1108 |
| q754_FantasticDungeons1stQuestBonVoyage | FantasticDungeons.qst | 2.55 or 3.0 Nightly | All screens visited | 499 of 499 |
| q755_ShadowblazersTinyQuestGoldVersion | Shadowblazer-Tiny-Gold-255_0.qst | 2.55 or 3.0 Nightly | All screens visited | 114 of 114 |
| q756_Imzogelmos5thQuestCandidate | imzo_5th.qst | 2.50.1 | All screens visited | 472 of 472 |
| q757_RemnantsoftheDarkKing | RotDK.qst | 2.55 or 3.0 Nightly | All screens visited | 143 of 143 |
| q758_MatthewBluefoxQuestPackDeutsch | 1stHM_De.qst | 2.53.1 | All screens visited | 530 of 530 |
| q758_MatthewBluefoxQuestPackDeutsch | 1stMini_De.qst | 2.53.1 | All screens visited | 318 of 318 |
| q758_MatthewBluefoxQuestPackDeutsch | 2ndHM_De.qst | 2.53.1 | All screens visited | 528 of 528 |
| q758_MatthewBluefoxQuestPackDeutsch | 2ndMini_De.qst | 2.53.1 | All screens visited | 330 of 330 |
| q758_MatthewBluefoxQuestPackDeutsch | 3rdHM_De.qst | 2.53.1 | All screens visited | 599 of 599 |
| q758_MatthewBluefoxQuestPackDeutsch | LOZRandyMaxi_De.qst | 2.53.1 | All screens visited | 1813 of 1813 |
| q759_TheZeldaLegend1984Arcade | TheZeldaLegend.qst | 2.50.2 | All screens visited | 130 of 130 |
| q760_ZeldaNESRemasteredVersion20255Version | zeldanesremastered_V2_0.qst | 2.55 or 3.0 Nightly | All screens visited | 1546 of 1546 |
| q761_WanderlustAgamenooneplays | 2_Wanderlust-The.game.no.one.playsEN.qst | 2.55 or 3.0 Nightly | Partly visited (time limit) | 6117 of 12237 |
| q761_WanderlustAgamenooneplays | Wanderlust-The.game.no.one.playsEN.qst | 2.53.1 | Partly visited (time limit) | 4446 of 12235 |
| q761_WanderlustAgamenooneplays | Wanderlust-The.game.no.one.playsFR.qst | 2.53.1 | Partly visited (time limit) | 4742 of 12235 |
| q762_TitansGate2 | TitansGate2V1.15.qst | 2.55 or 3.0 Nightly | All screens visited | 1208 of 1208 |
| q763_RemembranceofShadows | remembrance255.qst | 2.55 or 3.0 Nightly | All screens visited | 2268 of 2268 |
| q764_BowsersQuest | Bowser's Quest.qst | 2.53.1 | All screens visited | 983 of 983 |
| q765_ArmageddonQuest2 | AQ2.qst | 2.53.1 | All screens visited | 1011 of 1011 |
| q766_Anormalz1remake | Anormalz1remake.qst | 2.53.1 | All screens visited | 32 of 32 |
| q767_Bobsquest2 | Bob's quest 2.qst | 2.53.1 | All screens visited | 527 of 527 |
| q768_LinksDecision2ChronostheTearsofFate | Links.Decision.2.qst | 2.10 | All screens visited | 4138 of 4138 |
| q769_LanguageSexViolenceOtherExateEdition | lsvo.qst | 2.11 | All screens visited | 841 of 841 |
| q770_DemoQuestCollectionforVersion255 | demo255.qst | 2.55 or 3.0 Nightly | All screens visited | 346 of 346 |
| q770_DemoQuestCollectionforVersion255 | demoex255.qst | 2.55 or 3.0 Nightly | All screens visited | 582 of 582 |
| q770_DemoQuestCollectionforVersion255 | demosp255.qst | 2.55 or 3.0 Nightly | All screens visited | 651 of 651 |
| q770_DemoQuestCollectionforVersion255 | demozx255.qst | 2.55 or 3.0 Nightly | All screens visited | 653 of 653 |
| q771_FazyFunArcade10 | Freddy'sFUNARCADE.qst | 2.55 or 3.0 Nightly | All screens visited | 32 of 32 |
| q772_100RoomsofWisdom2 | 100RoomsWisdom2.qst | 2.53.1 | All screens visited | 154 of 154 |
| q773_1stQuestMirrored | 1st-mirrored-horizontal.qst | 2.53.0 | All screens visited | 869 of 869 |
| q773_1stQuestMirrored | 1st-mirrored-vertical-and-horizontal.qst | 2.53.0 | All screens visited | 869 of 869 |
| q773_1stQuestMirrored | 1st-mirrored-vertical.qst | 2.53.0 | All screens visited | 869 of 869 |
| q773_1stQuestMirrored | quest.qst | unknown | Refused: not a recognised quest file |  |
| q774_StellarSeas | StellarSeas.qst | 2.55 or 3.0 Nightly | All screens visited | 1106 of 1106 |
| q775_PlanetTelex | PlanetTelex.qst | 2.50.1 | All screens visited | 123 of 123 |
| q776_DuelingDragons | DuelingDragons.qst | 2.55 or 3.0 Nightly | All screens visited | 139 of 139 |
| q777_HopelessTowerAscent | HopelessTower.qst | 2.50.1 | All screens visited | 65 of 65 |
| q778_MirrorTower | mirrortower.qst | 2.55 or 3.0 Nightly | All screens visited | 237 of 237 |
| q779_TheColossalTower | TheColossalTower.qst | 2.55 or 3.0 Nightly | All screens visited | 530 of 530 |
| q780_ChaosIsland | Chaos_Island_4_21_Nightly.qst | 2.55 or 3.0 Nightly | All screens visited | 128 of 128 |
| q781_CanariatheUpwindMarch | Canaria, the Upwind March.qst | 2.53.1 | All screens visited | 1633 of 1633 |
| q782_MysteriesofTheCupPuzzlesofHyruleOther | First GameBoy.qst | 2.53.1 | All screens visited | 199 of 199 |
| q782_MysteriesofTheCupPuzzlesofHyruleOther | Mysteries of the Cup.qst | 2.50.2 | All screens visited | 36 of 36 |
| q782_MysteriesofTheCupPuzzlesofHyruleOther | Puzzles of Hyrule.qst | 2.53.1 | All screens visited | 228 of 228 |
| q783_RoeblozsWorldAdventure3KnilStrikesBack | RWA_3.qst | 2.53.1 | All screens visited | 2175 of 2175 |
| q784_DreamyCambria | Dreamy Cambria.qst | 2.55 or 3.0 Nightly | All screens visited | 286 of 286 |
| q785_CambriaTheLostWorlds | Cambria The Lost Worlds.qst | 2.55 or 3.0 Nightly | All screens visited | 1577 of 1577 |
| q786_FlameofRevenge | Flame_of_Revenge.qst | 2.55 or 3.0 Nightly | All screens visited | 163 of 163 |
| q787_RitesofLineage | Rites_Of_Lineage.qst | 2.55 or 3.0 Nightly | All screens visited | 1209 of 1209 |
| q788_TwinTraditions | TwinTraditions.qst | 2.55 or 3.0 Nightly | All screens visited | 230 of 230 |
| q789_UnderanhourZeldaUZ | BananaPudding.qst | 2.53.1 | All screens visited | 134 of 134 |
| q790_LinksGetaway | LinksGetaway.qst | 2.55 or 3.0 Nightly | All screens visited | 174 of 174 |
| q791_NightandDay | Night and Day.qst | 2.53.1 | All screens visited | 496 of 496 |
| q792_MitchesFaultII | MITCHES FAULT II.qst | 2.55 or 3.0 Nightly | All screens visited | 144 of 144 |
| q793_ZeldaOverturev17481123 | Zelda Overture 1.7.4.qst | 2.55 or 3.0 Nightly | All screens visited | 383 of 383 |
| q794_ThePowerWalrus | PWRwalrus.qst | 2.55 or 3.0 Nightly | All screens visited | 329 of 329 |
| q795_OneScreenGollab | OneScreenGollab.qst | 2.55 or 3.0 Nightly | Partly visited (time limit) | 961 of 993 |
| q796_OtherworldlyPlanet | otherworldlyplanet.qst | 2.55 or 3.0 Nightly | All screens visited | 153 of 153 |
| q797_ZeldaOvertureIIv1148223 | Zelda Overture II 1.1.4.qst | 2.55 or 3.0 Nightly | All screens visited | 443 of 443 |
| q798_MrReDeadsTutorialQuest | Tutorial.qst | 2.53.1 | All screens visited | 124 of 124 |
| q799_BSisBS | BSisBS.qst | 2.53.1 | All screens visited | 776 of 776 |
| q800_TheLegendofZeldaHeroMasterMode | 1stHM.qst | 2.55 or 3.0 Nightly | All screens visited | 530 of 530 |
| q800_TheLegendofZeldaHeroMasterMode | 1stMM.qst | 2.55 or 3.0 Nightly | All screens visited | 530 of 530 |
| q800_TheLegendofZeldaHeroMasterMode | 2ndHM.qst | 2.55 or 3.0 Nightly | All screens visited | 530 of 530 |
| q800_TheLegendofZeldaHeroMasterMode | 2ndMM.qst | 2.55 or 3.0 Nightly | All screens visited | 530 of 530 |
| q801_TheLegendofZeldaLostLevels | 1stLL.qst | 2.55 or 3.0 Nightly | All screens visited | 318 of 318 |
| q801_TheLegendofZeldaLostLevels | 2ndLL.qst | 2.55 or 3.0 Nightly | All screens visited | 330 of 330 |
| q801_TheLegendofZeldaLostLevels | 3rdLL.qst | 2.55 or 3.0 Nightly | All screens visited | 364 of 364 |
| q802_TheLegendofZeldaCrazyUltimateChallenge | CC.qst | 2.55 or 3.0 Nightly | All screens visited | 599 of 599 |
| q802_TheLegendofZeldaCrazyUltimateChallenge | UC.qst | 2.55 or 3.0 Nightly | All screens visited | 1907 of 1907 |
| q803_JourneyintotheNovelRuins | NovelRuins1.2.qst | 2.55 or 3.0 Nightly | All screens visited | 915 of 915 |
| q804_DreamsRealm | Dreams_Realm 2.53.qst | 2.53.1 | Partly visited (time limit) | 3815 of 8021 |
| q804_DreamsRealm | Dreams_Realm 2.55 NEW.qst | 2.55 or 3.0 Nightly | Partly visited (time limit) | 4833 of 8220 |
| q805_LinksGrandAdventure3Remastered | Link's Grand Adventure 3 Remastered.qst | 2.55 or 3.0 Nightly | All screens visited | 419 of 419 |
| q806_LinksAdventureintheFuture | Links_Adventure_in_the_Future.qst | 2.53.1 | All screens visited | 2975 of 2975 |
| q807_TheLegendofNebulstarCanceledQuest | The Legend of Nebulstar.qst | 2.53.1 | All screens visited | 477 of 477 |
| q808_TheEnigmaofBasilischiIsland | enigmaofbasilischi.qst | 2.55 or 3.0 Nightly | All screens visited | 340 of 340 |
| q809_GarbysGarbagePuzzleGame | Garby's Garbage Puzzle Game.qst | 2.55 or 3.0 Nightly | All screens visited | 161 of 161 |
| q810_TheDeep | The Deep.qst | 2.55 or 3.0 Nightly | All screens visited | 326 of 326 |
| q811_0thQuest | 0th.qst | 2.50.2 | All screens visited | 263 of 263 |
| q812_TheCycle | The Cycle.qst | 2.53.1 | All screens visited | 1314 of 1314 |
| q813_WinningTheLottery | WinningTheLottery.qst | 2.53.1 | All screens visited | 14 of 14 |
| q814_TalesofZelsteriaV25 | Tales of Zelsteria.qst | 2.55 or 3.0 Nightly | All screens visited | 463 of 463 |
| q815_InfiniteQuest | infinite.qst | 2.55 or 3.0 Nightly | All screens visited | 451 of 451 |
| q816_LanaAgainPostContestVersion | Lana Again.qst | 2.55 or 3.0 Nightly | All screens visited | 406 of 406 |
| q817_NovelRuinsGaidenTheSeleneMountain | NovelRuinsGaiden1.12.qst | 2.55 or 3.0 Nightly | All screens visited | 485 of 485 |
| q818_100RoomsofWisdom3 | 100RoomsWisdom3.qst | 2.53.1 | All screens visited | 694 of 694 |
| q819_LinksRise2DX | LR2DX.qst | 2.55 or 3.0 Nightly | All screens visited | 1289 of 1289 |
| q820_IridiumOFFENSIVE | Iridium OFFENSIVE.qst | 2.53.1 | All screens visited | 152 of 152 |
| q821_HyruleFantasyGaidenSpellofMudora2ndQuest | Spell_of_Mudora_v2+Q2.qst | 2.55 or 3.0 Nightly | All screens visited | 968 of 968 |
| q823_NostalrulPlus | NostalrulPlus.qst | 2.55 or 3.0 Nightly | All screens visited | 815 of 815 |
| q824_KirbssCastleEpic | kingdom.qst | 2.55 or 3.0 Nightly | All screens visited | 216 of 216 |
| q825_SaintJamesPlace | SaintJamesPlaceASide.qst | 2.55 or 3.0 Nightly | All screens visited | 383 of 383 |
| q825_SaintJamesPlace | SaintJamesPlaceBSide.qst | 2.55 or 3.0 Nightly | All screens visited | 235 of 235 |
| q826_Pacman | PacMan.qst | 2.55 or 3.0 Nightly | All screens visited | 103 of 103 |
| q827_FaceHunt | Face_Hunt_V1_3_5.qst | 2.55 or 3.0 Nightly | All screens visited | 993 of 993 |
| q828_NestasticGaidenContestRelease | Nestastic Gaiden - Contest Version.qst | 2.55 or 3.0 Nightly | All screens visited | 1009 of 1009 |
| q829_LegendofZeldaTalesofMurdoch | TalesofMurdoch.qst | 2.55 or 3.0 Nightly | All screens visited | 718 of 718 |
| q830_TheDekuWithin | The Deku Within 1_1.qst | 2.55 or 3.0 Nightly | All screens visited | 621 of 621 |
| q831_LegendofZeldaLinksJourneyNorth | Link'sJourneyNorth.qst | 2.55 or 3.0 Nightly | All screens visited | 551 of 551 |
| q832_Magequest2 | Magequest_2_RELEASE.qst | 2.50.2 | All screens visited | 99 of 99 |
| q833_barato | barato.qst | 2.55 or 3.0 Nightly | All screens visited | 1 of 1 |
| q835_SoldiersTest | Soldier's Test V1_1.qst | 2.55 or 3.0 Nightly | All screens visited | 86 of 86 |
| q836_Darkside | Darkside.qst | 2.55 or 3.0 Nightly | All screens visited | 84 of 84 |
| q837_JacksQuest | Jacks.qst | 2.55 or 3.0 Nightly | All screens visited | 541 of 541 |
| q837_JacksQuest | quest.qst | unknown | Refused: not a recognised quest file |  |
| q838_Canvas64 | Canvas 64.qst | 2.55 or 3.0 Nightly | All screens visited | 84 of 84 |
| q839_TheLegendofRevaliHatesEnd | The Legend of Revali.qst | 2.53.1 | Partly visited (time limit) | 2598 of 5579 |
| q840_ThisisTheOnlyRoom | onlyroom.qst | 2.55 or 3.0 Nightly | All screens visited | 63 of 63 |
| q841_TheLegendofZeldaLinksJourneyNorthDX | LJNdx.qst | 2.55 or 3.0 Nightly | All screens visited | 844 of 844 |
| q842_Mickensie | Mickensie.qst | 2.53.1 | All screens visited | 664 of 664 |
| q843_MickensieII | Mickensie II.qst | 2.53.1 | All screens visited | 607 of 607 |
| q844_MickensieIII | Mickensie III.qst | 2.53.1 | All screens visited | 586 of 586 |
| q845_TheOfficial7thQuest | 7th.qst | 2.53.1 | All screens visited | 871 of 871 |
| q846_FreedominChains | FreedomInChains.qst | 2.53.1 | All screens visited | 338 of 338 |
| q847_HollowForest | Hollow Forest.qst | 2.53.1 | All screens visited | 278 of 278 |
| q848_NargadsTrailtheDoomedKingdom | NTTDK.qst | 2.55 or 3.0 Nightly | All screens visited | 89 of 89 |
| q849_CambriaDangerintheDarkworldPostcontestversion | Cambria Danger in the Darkworld.qst | 2.55 or 3.0 Nightly | All screens visited | 462 of 462 |
| q850_ZeldasExcellentJourney | Zelda's Excellent Journey.qst | 2.53.1 | All screens visited | 601 of 601 |
| q851_FunintheSunFullCircle | FCIRCLE.qst | 1.90 | All screens visited | 484 of 484 |
| q852_NitaGaiden | NitaGaiden.qst | 2.55 or 3.0 Nightly | All screens visited | 128 of 128 |
| q853_MetdroidPlanetXX32 | M-3-T-5-0-1-D.qst | 2.53.1 | Partly visited (time limit) | 2889 of 4099 |
| q854_TheStarFilledVoid | The Star Filled Void.qst | 2.55 or 3.0 Nightly | All screens visited | 469 of 469 |
| q855_OcarinaofTime1986 | OOT1986.qst | 2.55 or 3.0 Nightly | All screens visited | 1174 of 1174 |
| q856_TheRevival | REVIVAL.QST | 1.90 | All screens visited | 736 of 736 |
| q857_TranquilExpression | Tranquil Expression.qst | 2.55 or 3.0 Nightly | All screens visited | 1827 of 1827 |
| q858_HyruleWorld20 | Hyrule World2.0.qst | 2.55 or 3.0 Nightly | All screens visited | 1537 of 1537 |
| q859_TheROSEMASTER | theRosemaster.qst | 2.55 or 3.0 Nightly | Partly visited (time limit) | 3275 of 10660 |
| q860_ShadowKnight | ShadowKnight.qst | 2.55 or 3.0 Nightly | All screens visited | 514 of 514 |

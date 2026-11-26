ZQuest's per-build interface profiles (src/port/zqeditor/zq_ui_profile.h)
==========================================================================

One file per generation, registering that generation's profiles before main():

    src/port/zqeditor/zq_ui_profiles_190.cpp   1.90, 1.92, 2.10.x, 2.11 betas   (gen 0x190 ... 0x211)
    src/port/zqeditor/zq_ui_profiles_250.cpp   2.50.x, 2.53.0                   (gen 0x250)
    src/port/zqeditor/zq_ui_profiles_255.cpp   2.55.0 ... 2.55.19               (gen 0x255)
    src/port/zqeditor/zq_ui_profiles_300.cpp   3.0.0-prerelease.N               (gen 0x300)

A profile's build is the catalog's name for it, as File > New lists it: "1.90", "1.92 (Beta 114)",
"2.10.3", "2.11 Beta 6", "2.50.2", "2.55.19", "3.0 Nightly 2026-11-06" (3.0 builds are listed by
date, one entry for each run of prereleases with the same format). Betas the catalog names only while
such a quest is open ("2.11 Beta 12", "1.92 Beta 108") are ordered as the catalog orders them then.
2.53.1 is today's interface and stays the framework's own (a registered "2.53.1" is left out).

src/port/zqeditor.cmake picks up every zq_ui_profiles_*.cpp by itself (a glob): add no CMake line.
zq_ui_profile.cpp (the framework) registers 2.53.1 (today's interface, the default) and, until
the generation files arrive, two hand-written profiles in
zq_ui_profiles_hand.h ("2.10.3", "3.0 Nightly 2026-11-06"); a generation file registering the
same build replaces the hand-written one.

A file looks like this:

    #include "precompiled.h" //always first
    #include "zq_ui_profile.h"

    namespace
    {
    const zq_ui_menu_row menu_2553[] =
    {
        { 0, "&File", NULL },
        { 1, "&New", "File/New" },
        { 1, "&Open\tF3", "File/Open" },
        { 1, "", NULL },                                  // a separator
        { 1, "&Import\t ", NULL },                        // a submenu: the rows below, one deeper
        { 2, "&Map", "File/Import/Map" },
        { 1, "Open &File...", "" },                       // no function here: shown disabled
        { 0, "&Quest", NULL },
        { 1, "&ZInfo", "Quest/2.55/3.0 ZInfo...", 0x255 }, // disabled for quests older than 2.55
        ...
    };
    const zq_ui_rect rects_2553[] = { { "panel", -1, 64, ZQ_UI_KEEP, 192, ZQ_UI_KEEP } };
    const zq_ui_layout layout_2553 = { "2.55.3", 1, 0, 0, rects_2553, 1 };
    const zq_ui_look look_2553 = { "2.55.3", false,
        { 0, 0x000000, 0x424142, 0x848284, 0xD4D0C8, 0xFFFFFF, 0xFFFFE1, 0xFFE1A0, 0x000050 },
        0x0A246A, 0xA6CAF0, 7,
        { 4, 5, 4, 3, 2, 1, 9, 15, 7, 5, 1, 8, 6 }, 1, 0 };
    const zq_ui_profile p2553 = { "2.55.3", 0x255, menu_2553, sizeof(menu_2553) / sizeof(menu_2553[0]),
                                  &layout_2553, &look_2553, NULL, 0 };

    void add()
    {
        zq_ui_add_profile(&p2553);
        // zq_ui_add_action("@myaction", onMyAction, 0x255);   // an extra editor function, if needed
    }
    zq_ui_registrar reg(add);
    } // namespace

Rules
- Actions are today's (2.53.1) menu paths as the menu audit writes them: zqeditor's auditlist
  (ZQ_UI_SCRIPT + ZQ_AUDIT_LOG) prints every one ("entry <path>"). The framework's extras:
  "@interface" (Etc > Interface...), "@enemy_flags" (2.10's Data > Enemy Flags: Screen Data's
  enemy tab), "@pool", "@auto" (the Pool and Auto Combo drawing modes, 2.55/3.0 quests).
  A path that doesn't exist is shown disabled and written to allegro.log once.
- Shortcuts: the text after '\t' (F1-F12, Del) is bound to that entry's action while the profile is
  shown (2.10's F5 runs the NES Dungeon Template, F12 Enemy Flags); other keys stay today's.
- Every profile gets the bar's Interface menu at its end (added by the framework, not in the rows);
  put "Interface..." with action "@interface" in the build's Etc (or Options) menu, after Options.
- needs: 0x255 or 0x300 for entries that edit what only 2.55 / 3.0 quests hold; the framework
  disables them under that interface for older quests (actions such as "Quest/2.55/3.0 ZInfo..."
  already carry it).
- Profiles cover builds: a quest shows the profile of its own build, else the newest profile of its
  generation made before its build, else the oldest of its generation, else 2.53.1's.
- Look colours are 8-bit 0xRRGGBB (the framework converts to the 6-bit palette as the old ZQuests
  did, c*63/255). keep=true only for 2.53.1-like profiles that show the user's own colours.
- No jwin or Allegro code: data only, plus at most an extra action calling an editor function.

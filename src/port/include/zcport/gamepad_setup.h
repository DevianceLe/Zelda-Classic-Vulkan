/*
 * zplayer's Settings > Controls > Gamepad Setup..., for the game code (src/zc_sys.cpp).
 * Implemented in src/port/game/ui_gamepad.cpp (Dear ImGui), not on the Switch.
 *
 * Every pad SDL knows reads in one layout (src/port/platform/platform_sdl.cpp), the one the
 * game's default joystick buttons are made for. This window lists the connected pads, shows
 * what each one is pressing, and sets up a pad SDL doesn't know (a PlayStation adapter, an old
 * USB pad): it asks for each button in turn and keeps the answer as an SDL gamepad mapping in
 * gamecontrollerdb.txt beside zc.cfg, so from then on that pad reads in the same layout, in
 * the game and in the menus.
 *
 * Game thread only. C++ only; includes neither SDL nor ImGui.
 */

#ifndef ZCPORT_GAMEPAD_SETUP_H
#define ZCPORT_GAMEPAD_SETUP_H

/* Shows the window as a modal UI session and returns when the player closes it. */
void zplayer_gamepad_setup();

#endif /* ZCPORT_GAMEPAD_SETUP_H */

/*
 * zplayer's Video Settings, for the game code (src/zc_sys.cpp,
 * src/zelda.cpp). Implemented in src/port/game/ui_video.cpp on top of
 * zcport/display.h (window side) and zcport/ui.h (Dear ImGui).
 *
 * Game thread only (they read and write the Allegro config).
 *
 * zc.cfg: [zeldadx] fullscreen (windowed or fullscreen) and window_display
 * (the monitor, 1 = leftmost) keep their meaning; [zplayer] holds the rest:
 *   fullscreen_mode    borderless | exclusive   (what fullscreen means)
 *   window_width/height                         (0 = the game's screen size)
 *   fullscreen_width/height, fullscreen_refresh (exclusive; 0 = desktop mode)
 *   scaling            integer | fit | smooth | stretch
 *   vsync, show_fps    0 | 1
 *   ui_scale           percent (100)
 *
 * C++ only; includes neither SDL nor ImGui.
 */

#ifndef ZCPORT_VIDEO_SETTINGS_H
#define ZCPORT_VIDEO_SETTINGS_H

/* Settings > Video Settings...: shows the settings window as a modal UI
 * session and returns when the player closes it. The caller saves the
 * config afterwards (save_game_configs). */
void zplayer_video_settings();

/* Writes the display settings to the config (from save_game_configs()):
 * the [zplayer] section, window_display, and the game's 'fullscreen' flag
 * when the player switched between windowed and fullscreen this run. */
void zplayer_video_save_config();

/* Keeps the window at its present size across a change of the game's screen
 * size (ZQuest's Large and Small layouts). */
void zplayer_video_pin_window();

/* The window is fullscreen (borderless or exclusive). */
int zplayer_window_is_fullscreen();

/* Windowed <-> the chosen fullscreen kind, without a new graphics mode
 * (the game's Misc > Fullscreen; Alt+Enter does the same). */
void zplayer_toggle_fullscreen();

#endif /* ZCPORT_VIDEO_SETTINGS_H */

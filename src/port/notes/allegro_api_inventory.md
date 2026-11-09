# Allegro API used by the player

Generated from the player's sources (CMake lists for the `zelda` target, zcmusic, all `src/*.h`).
Kind: f = function, i = inline function, v = variable (or struct member caught by the scan), m = macro, t = type, e = enum.
Counts are references in the source. A few entries are false matches (struct members such as `proc`, `name`, `move`).

## allegro/3d.h
`V3D_f` (t 14)

## allegro/alcompat.h
`OLD_FILESEL_HEIGHT` (m 6), `OLD_FILESEL_WIDTH` (m 3), `SEND_MESSAGE` (m 5), `callback` (v 6)

## allegro/base.h
`FALSE` (m 48), `MAX` (m 12), `MID` (m 6), `MIN` (m 8), `TRUE` (m 41), `allegro_errno` (v 3)

## allegro/color.h
`bestfit_color` (f 1), `create_rgb_table` (f 3), `fade_interpolate` (f 15), `get_color` (f 1), `get_palette` (f 4), `makecol` (f 25), `set_color` (f 4), `set_palette` (f 11), `set_palette_range` (f 8), `set_trans_blender` (f 1), `COLOR_MAP` (t 12), `RGB_MAP` (t 11), `black_palette` (v 19), `palette_color` (v 38), `rgb_map` (v 3)

## allegro/compiled.h
`RLE_SPRITE` (t 4)

## allegro/config.h
`flush_config_file` (f 1), `get_config_int` (f 177), `get_config_string` (f 4), `get_config_text` (f 6), `set_config_file` (f 3), `set_config_int` (f 92), `set_config_string` (f 3)

## allegro/datafile.h
`load_datafile` (f 3), `register_bitmap_file_type` (f 1), `save_bitmap` (f 4), `save_bmp` (f 1), `DAT_BITMAP` (m 1), `DAT_ID` (m 1), `DAT_RLE_SPRITE` (m 1), `DATAFILE` (t 12), `load` (v 2), `save` (v 1)

## allegro/debug.h
`al_trace` (f 113), `register_trace_handler` (f 1), `ASSERT` (m 118)

## allegro/digi.h
`allocate_voice` (f 3), `deallocate_voice` (f 3), `play_sample` (f 1), `stop_sample` (f 1), `voice_get_position` (f 3), `voice_set_pan` (f 2), `voice_set_playmode` (f 3), `voice_set_position` (f 2), `voice_set_volume` (f 2), `voice_start` (f 7), `voice_stop` (f 11), `DIGI_AUTODETECT` (m 1), `PLAYMODE_LOOP` (m 3), `PLAYMODE_PLAY` (m 2), `SAMPLE` (t 11), `exit` (v 22), `get_volume` (v 2), `init` (v 23), `set_pan` (v 2), `set_volume` (v 6)

## allegro/draw.h
`blit` (f 127), `masked_blit` (f 17), `masked_stretch_blit` (f 2), `solid_mode` (f 3), `stretch_blit` (f 32), `stretch_sprite` (f 18), `DRAW_MODE_COPY_PATTERN` (m 3), `DRAW_MODE_SOLID` (m 9), `DRAW_MODE_TRANS` (m 9)

## allegro/file.h
`append_filename` (f 6), `canonicalize_filename` (f 2), `delete_file` (f 20), `exists` (f 34), `file_exists` (f 5), `file_size_ex` (f 1), `fix_filename_case` (f 6), `fix_filename_slashes` (f 5), `for_each_file_ex` (f 2), `get_extension` (f 6), `get_filename` (f 14), `is_relative_filename` (f 2), `make_relative_filename` (f 4), `pack_fclose` (f 28), `pack_feof` (f 9), `pack_ferror` (f 11), `pack_fopen` (f 1), `pack_fread` (f 8), `pack_fseek` (f 2), `pack_fwrite` (f 4), `pack_getc` (f 11), `pack_igetl` (f 1), `pack_igetw` (f 1), `pack_iputl` (f 1), `pack_iputw` (f 1), `pack_mgetl` (f 1), `pack_mgetw` (f 1), `pack_mputl` (f 3), `pack_mputw` (f 4), `pack_putc` (f 1), `packfile_password` (f 9), `put_backslash` (f 6), `replace_extension` (f 4), `replace_filename` (f 7), `EOF` (m 22), `F_READ` (m 9), `F_READ_PACKED` (m 4), `F_WRITE` (m 1), `F_WRITE_PACKED` (m 1), `PACKFILE_FLAG_WRITE` (m 22), `PACKFILE` (t 130)

## allegro/fix.h
`fix` (t 810)

## allegro/fixed.h
`fixed` (t 34)

## allegro/font.h
`FONT` (t 262)

## allegro/gfx.h
`clear_bitmap` (f 95), `create_bitmap_ex` (f 58), `create_sub_bitmap` (f 12), `create_video_bitmap` (f 1), `destroy_bitmap` (f 53), `enable_triple_buffer` (f 1), `get_color_depth` (f 9), `poll_scroll` (f 1), `request_video_bitmap` (f 1), `set_clip_rect` (f 27), `set_color_conversion` (f 1), `set_color_depth` (f 12), `set_display_switch_callback` (f 2), `set_display_switch_mode` (f 2), `set_gfx_mode` (f 5), `vsync` (f 15), `COLORCONV_NONE` (m 1), `GFX_AUTODETECT` (m 1), `GFX_AUTODETECT_FULLSCREEN` (m 2), `GFX_AUTODETECT_WINDOWED` (m 5), `GFX_CAN_TRIPLE_BUFFER` (m 2), `GFX_HW_CURSOR` (m 2), `GFX_SAFE` (m 1), `GFX_TEXT` (m 2), `SCREEN_H` (m 21), `SCREEN_W` (m 20), `SWITCH_BACKAMNESIA` (m 2), `SWITCH_BACKGROUND` (m 2), `SWITCH_IN` (m 1), `SWITCH_OUT` (m 1), `arc` (v 5), `circle` (v 3), `circlefill` (v 7), `clear_to_color` (v 29), `draw_rle_sprite` (v 3), `draw_sprite` (v 6), `draw_sprite_v_flip` (v 1), `draw_trans_sprite` (v 24), `ellipse` (v 2), `ellipsefill` (v 3), `floodfill` (v 1), `getpixel` (v 4), `gfx_capabilities` (v 4), `gfx_driver` (v 1), `hline` (v 3), `line` (v 179), `polygon` (v 1), `putpixel` (v 26), `quad3d_f` (v 2), `rect` (v 18), `rectfill` (v 117), `screen` (v 418), `scroll` (v 6), `set_mouse_sprite` (v 1), `show_mouse` (v 10), `spline` (v 1), `triangle` (v 1), `triangle3d_f` (v 2), `vline` (v 8)

## allegro/gui.h
`broadcast_dialog_message` (f 11), `d_bitmap_proc` (f 4), `d_button_proc` (f 3), `d_check_proc` (f 3), `d_ctext_proc` (f 6), `d_edit_proc` (f 1), `d_keyboard_proc` (f 21), `d_list_proc` (f 1), `d_text_proc` (f 3), `d_yield_proc` (f 2), `do_dialog` (f 2), `gui_get_screen` (f 2), `gui_set_screen` (f 2), `gui_strlen` (f 7), `gui_textout_ex` (f 9), `init_dialog` (f 7), `object_message` (f 38), `popup_dialog` (f 2), `set_dialog_color` (f 1), `shutdown_dialog` (f 5), `update_dialog` (f 5), `D_CLOSE` (m 32), `D_DIRTY` (m 9), `D_DISABLED` (m 61), `D_EXIT` (m 89), `D_GOTFOCUS` (m 14), `D_HIDDEN` (m 7), `D_INTERNAL` (m 3), `D_O_K` (m 183), `D_REDRAW` (m 6), `D_SELECTED` (m 156), `D_USED_CHAR` (m 25), `D_USER` (m 9), `D_WANTFOCUS` (m 8), `MSG_CHAR` (m 9), `MSG_CLICK` (m 21), `MSG_DCLICK` (m 3), `MSG_DRAW` (m 75), `MSG_END` (m 2), `MSG_GOTMOUSE` (m 2), `MSG_IDLE` (m 11), `MSG_KEY` (m 16), `MSG_LOSTFOCUS` (m 2), `MSG_LOSTMOUSE` (m 1), `MSG_LPRESS` (m 1), `MSG_LRELEASE` (m 1), `MSG_MPRESS` (m 1), `MSG_MRELEASE` (m 1), `MSG_RADIO` (m 2), `MSG_RPRESS` (m 1), `MSG_RRELEASE` (m 1), `MSG_START` (m 20), `MSG_UCHAR` (m 2), `MSG_USER` (m 1), `MSG_WANTFOCUS` (m 7), `MSG_WHEEL` (m 4), `MSG_XCHAR` (m 1), `DIALOG` (t 299), `DIALOG_PLAYER` (t 7), `MENU` (t 26), `DIALOG_PROC` (v 2), `active_menu` (v 3), `gui_bg_color` (v 4), `gui_fg_color` (v 3), `gui_font_baseline` (v 10), `gui_mg_color` (v 8), `gui_mouse_b` (v 50), `gui_mouse_focus` (v 2), `gui_mouse_x` (v 30), `gui_mouse_y` (v 33), `gui_mouse_z` (v 3)

## allegro/inline/color.inl
`getb15` (i 7), `getb8` (i 4), `getg15` (i 7), `getg8` (i 4), `getr15` (i 7), `getr8` (i 4), `makecol15` (i 4)

## allegro/inline/draw.inl
`_allegro_hline` (i 48), `_allegro_vline` (i 42), `rotate_sprite` (i 18)

## allegro/inline/fmaths.inl
`fixcos` (i 22), `fixdiv` (i 1), `fixsin` (i 23), `fixtoi` (i 47), `ftofix` (i 2), `itofix` (i 28)

## allegro/inline/gfx.inl
`acquire_bitmap` (i 1), `acquire_screen` (i 1), `bitmap_color_depth` (i 6), `is_same_bitmap` (i 1), `is_sub_bitmap` (i 1), `is_windowed_mode` (i 6), `release_bitmap` (i 1), `release_screen` (i 1), `set_clip_state` (i 22)

## allegro/internal/aintern.h
`_al_drive_exists` (f 3), `_al_free` (f 4), `_al_getdcwd` (f 3), `_al_getdrive` (f 3), `_al_malloc` (f 1), `_al_sane_realloc` (f 3), `_gfx_mode_set_count` (v 2), `_mouse_screen` (v 2), `char_length` (v 7)

## allegro/internal/alconfig.h
`DEVICE_SEPARATOR` (m 8), `END_OF_FUNCTION` (m 4), `FA_ARCH` (m 1), `FA_DIREC` (m 8), `FA_HIDDEN` (m 2), `FA_LABEL` (m 2), `FA_RDONLY` (m 2), `FA_SYSTEM` (m 1), `LOCK_DATA` (m 2), `LOCK_FUNCTION` (m 4), `OTHER_PATH_SEPARATOR` (m 12), `memcmp` (m 2), `stricmp` (m 38), `strupr` (m 2), `uint64_t` (m 3), `name` (v 109)

## allegro/joystick.h
`install_joystick` (f 1), `poll_joystick` (f 6), `JOY_TYPE_AUTODETECT` (m 1), `MAX_JOYSTICKS` (m 1), `joy` (v 27), `num_joysticks` (v 1)

## allegro/keyboard.h
`clear_keybuf` (f 36), `install_keyboard` (f 1), `keypressed` (f 9), `readkey` (f 5), `scancode_to_ascii` (f 1), `set_keyboard_rate` (f 1), `simulate_keypress` (f 9), `KB_CTRL_FLAG` (m 4), `KB_SHIFT_FLAG` (m 4), `KEY_0` (m 1), `KEY_1` (m 1), `KEY_2` (m 1), `KEY_3` (m 1), `KEY_4` (m 1), `KEY_5` (m 1), `KEY_6` (m 1), `KEY_7` (m 1), `KEY_8` (m 1), `KEY_A` (m 4), `KEY_ALT` (m 1), `KEY_ASTERISK` (m 1), `KEY_B` (m 2), `KEY_BACKSPACE` (m 3), `KEY_C` (m 3), `KEY_CLOSEBRACE` (m 1), `KEY_COMMA` (m 1), `KEY_COMMAND` (m 2), `KEY_D` (m 2), `KEY_DEL` (m 3), `KEY_DEL_PAD` (m 1), `KEY_DOWN` (m 13), `KEY_E` (m 1), `KEY_END` (m 9), `KEY_ENTER` (m 8), `KEY_ENTER_PAD` (m 2), `KEY_EQUALS` (m 1), `KEY_ESC` (m 15), `KEY_F` (m 3), `KEY_F1` (m 6), `KEY_F10` (m 6), `KEY_F11` (m 2), `KEY_F12` (m 4), `KEY_F2` (m 4), `KEY_F3` (m 2), `KEY_F4` (m 2), `KEY_F5` (m 2), `KEY_F6` (m 4), `KEY_F7` (m 6), `KEY_F8` (m 6), `KEY_F9` (m 6), `KEY_G` (m 5), `KEY_H` (m 2), `KEY_HOME` (m 9), `KEY_I` (m 2), `KEY_J` (m 1), `KEY_K` (m 2), `KEY_L` (m 4), `KEY_LCONTROL` (m 4), `KEY_LEFT` (m 11), `KEY_LSHIFT` (m 9), `KEY_M` (m 3), `KEY_MINUS` (m 1), `KEY_MINUS_PAD` (m 1), `KEY_N` (m 65), `KEY_O` (m 4), `KEY_OPENBRACE` (m 1), `KEY_P` (m 3), `KEY_PGDN` (m 8), `KEY_PGUP` (m 8), `KEY_PLUS_PAD` (m 1), `KEY_Q` (m 3), `KEY_R` (m 2), `KEY_RCONTROL` (m 4), `KEY_RIGHT` (m 12), `KEY_RSHIFT` (m 9), `KEY_S` (m 2), `KEY_SLASH_PAD` (m 1), `KEY_SPACE` (m 4), `KEY_STOP` (m 1), `KEY_T` (m 1), `KEY_TAB` (m 5), `KEY_TILDE` (m 2), `KEY_U` (m 1), `KEY_UP` (m 10), `KEY_V` (m 3), `KEY_W` (m 3), `KEY_X` (m 3), `KEY_Y` (m 1), `KEY_Z` (m 2), `key` (v 224), `key_shifts` (v 6), `three_finger_flag` (v 1)

## allegro/midi.h
`destroy_midi` (f 2), `midi_pause` (f 3), `midi_resume` (f 1), `midi_seek` (f 3), `play_midi` (f 1), `stop_midi` (f 6), `MIDI_AUTODETECT` (m 1), `MIDI_TRACKS` (m 4), `MIDI` (t 22), `midi_driver` (v 1), `midi_loop_end` (v 1), `midi_loop_start` (v 1), `midi_pos` (v 8)

## allegro/mouse.h
`install_mouse` (f 1), `position_mouse` (f 3), `position_mouse_z` (f 11), `scare_mouse` (f 63), `scare_mouse_area` (f 2), `unscare_mouse` (f 65), `position` (v 4)

## allegro/palette.h
`PAL_SIZE` (m 17), `RGB` (t 66)

## allegro/platform/astdint.h
`int16_t` (m 2), `int32_t` (m 2), `int8_t` (m 2), `uint16_t` (m 2), `uint32_t` (m 2), `uint8_t` (m 2)

## allegro/sound.h
`install_sound` (f 1)

## allegro/stream.h
`free_audio_stream_buffer` (f 1), `get_audio_stream_buffer` (f 1), `play_audio_stream` (f 1), `stop_audio_stream` (f 2), `AUDIOSTREAM` (t 11)

## allegro/system.h
`allegro_exit` (f 1), `allegro_message` (f 1), `get_executable_name` (f 2), `set_close_button_callback` (f 1), `allegro_init` (m 1), `allegro_error` (v 7), `assert` (v 22), `desktop_color_depth` (v 2), `func` (v 4), `message` (v 14), `set_window_title` (v 1), `yield_timeslice` (v 1)

## allegro/text.h
`text_height` (f 140), `text_length` (f 106), `textout_centre_ex` (f 14), `textout_ex` (f 141), `textout_right_ex` (f 3), `textprintf_ex` (f 31), `textprintf_right_ex` (f 2), `font` (v 245)

## allegro/timer.h
`install_int_ex` (f 4), `install_timer` (f 1), `remove_int` (f 2), `rest` (f 8), `rest_callback` (f 1), `BPS_TO_TIMER` (m 1), `MSEC_TO_TIMER` (m 1), `SECS_TO_TIMER` (m 1)

## allegro/unicode.h
`set_uformat` (f 3), `ugetat` (f 7), `uinsert` (f 1), `uisspace` (f 2), `uoffset` (f 1), `usetat` (f 2), `ustrcmp` (f 1), `ustricmp` (f 1), `ustrlen` (f 2), `ustrncmp` (f 1), `ustrpbrk` (f 3), `ustrsizez` (f 1), `ustrtok_r` (f 2), `ustrtol` (f 2), `ustrzcat` (f 1), `ustrzcpy` (f 6), `utolower` (f 8), `utoupper` (f 3), `uvszprintf` (f 31), `uwidth_max` (f 11), `U_ASCII` (m 3), `U_CURRENT` (m 11), `uconvert_ascii` (m 10), `ustrdup` (m 1), `ucwidth` (v 14), `ugetc` (v 40), `ugetx` (v 2), `ugetxc` (v 4), `uisok` (v 1), `usetc` (v 65), `uwidth` (v 18)

## bealleg.h
`color_map` (m 3), `drawing_mode` (m 21)

## macalleg.h
`BITMAP` (t 664)

## winalleg.h
`win_get_window` (f 2), `WIN32_LEAN_AND_MEAN` (m 2), `move` (v 96), `proc` (v 137), `switch_in` (v 1), `switch_out` (v 1)

## Struct fields the game reads directly

- `BITMAP`: `->line[y]` (123 uses), `->clip`, `.line`
- `FONT`: `->vtable->char_length` (5 uses)
- `PACKFILE`: `f->normal.flags & PACKFILE_FLAG_WRITE` (zdefs.h, Allegro 4.4 branch) or `f->flags` (older branch)
- `joy[]`: `.stick[].axis[].d1/d2`, `.stick[].axis[].pos`, `.button[].b`, `.num_buttons`
- `DATAFILE`: `[i].type` (212), `[i].dat` (55), `[i].size` (6)
- `MIDI`: `->divisions`, `->track[].data/len` (src/midi.cpp)
- `SAMPLE`: `.len`, `.loop_start`, `.loop_end` and the usual fields (zc_sys.cpp, zcmusic.cpp)

## Added by the replay (src/replay.cpp, docs/zplayer-replay.md)

Used only with `-record`/`-replay`, beyond the list above: `install_keyboard_hooks`,
`remove_keyboard`, `keyboard_callback` (assigned), `keyboard_lowlevel_callback`
(assigned), `simulate_keypress`, `clear_keybuf`, `remove_joystick`,
`num_joysticks` and `joy[]` (written), `poll_mouse`, `gui_mouse_x/y/z/b`
(assigned), `get_palette`, `save_bmp` (`-framedump`). The behaviour they must
have is listed in docs/zplayer-replay.md ("What zplayer has to match").

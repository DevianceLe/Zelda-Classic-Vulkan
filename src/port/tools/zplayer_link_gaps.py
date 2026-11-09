"""zplayer link gaps: which symbols the zplayer link is still missing, and
which zcport module owns each of them.

Build the zplayer target and keep the build log, then run this on it:

    cmake --build build-wt --config Release --target zplayer -- -m -v:minimal -nologo > link.log 2>&1
    python -I src/port/tools/zplayer_link_gaps.py link.log docs/zplayer-link-gaps.md

It reads the MSVC "unresolved external symbol" errors (LNK2019/LNK2001),
groups the symbols by the module that owns them (the table below) and
writes a Markdown report. Symbols no module owns are listed first: those
are the gaps to assign. Re-run after merging modules to see what is left.
Exit code: 0 when the report was written.

Written for zcport.
"""

import collections
import re
import sys

# ---- module scopes ------------------------------------------------------------
#
# (key, title, scope, names, patterns). The first module whose names contain
# the symbol, or one of whose patterns matches it, owns it.

MODULES = [
    ('foundation', 'foundation (committed)',
     'system, al_trace, memory, bitmaps, clipping, keyboard and mouse state',
     set('''install_allegro _install_allegro_version_check allegro_exit allegro_message
            allegro_error allegro_id al_trace register_trace_handler register_assert_handler
            get_executable_name set_close_button_callback set_window_title desktop_color_depth
            get_desktop_resolution yield_timeslice check_cpu cpu_vendor cpu_family cpu_model
            cpu_capabilities os_type os_version os_revision os_multitasking system_driver
            set_display_switch_mode get_display_switch_mode set_display_switch_callback
            remove_display_switch_callback create_bitmap create_bitmap_ex create_sub_bitmap
            create_system_bitmap destroy_bitmap set_clip_rect get_clip_rect add_clip_rect
            set_clip_state get_clip_state is_same_bitmap is_sub_bitmap is_linear_bitmap
            is_memory_bitmap is_screen_bitmap is_video_bitmap is_system_bitmap bitmap_color_depth
            bitmap_mask_color acquire_bitmap release_bitmap acquire_screen release_screen
            set_color_depth get_color_depth set_color_conversion get_color_conversion screen
            _color_depth _color_conv _get_vtable install_keyboard remove_keyboard key key_shifts
            keypressed readkey ureadkey clear_keybuf simulate_keypress simulate_ukeypress
            set_keyboard_rate scancode_to_ascii scancode_to_name three_finger_flag
            keyboard_needs_poll poll_keyboard install_mouse remove_mouse mouse_x mouse_y mouse_z
            mouse_b mouse_pos poll_mouse show_mouse scare_mouse scare_mouse_area unscare_mouse
            position_mouse position_mouse_z set_mouse_range set_mouse_sprite
            set_mouse_sprite_focus show_os_cursor _mouse_screen _mouse_installed'''.split()),
     [r'^_al_(malloc|free|realloc|strdup|sane_realloc|sane_strncpy|srand|rand)$',
      r'^_(add|remove)_exit_func$', r'^zcport_(set|get)_platform$', r'^zcport_pump_events$']),

    ('fixmath', 'fixmath (committed)', 'fixed-point math',
     set('''fixsin fixcos fixtan fixasin fixacos fixatan fixatan2 fixsqrt fixhypot fixmul fixdiv
            fixadd fixsub fixtoi fixtof ftofix itofix fixceil fixfloor _cos_tbl _tan_tbl
            _acos_tbl'''.split()), []),

    ('prim', 'prim', 'drawing primitives',
     set('''putpixel getpixel _putpixel _putpixel15 _putpixel16 _putpixel24 _putpixel32
            _getpixel _getpixel15 _getpixel16 _getpixel24 _getpixel32 line fastline hline vline
            _allegro_hline _allegro_vline rect rectfill circle circlefill ellipse ellipsefill arc
            spline calc_spline floodfill polygon triangle clear_to_color clear_bitmap clear
            do_line do_circle do_ellipse do_arc drawing_mode xor_mode solid_mode
            _drawing_mode _drawing_pattern _drawing_x_anchor _drawing_y_anchor _drawing_x_mask
            _drawing_y_mask'''.split()), []),

    ('blit', 'blit', 'blits, sprites, RLE sprites, 3D polygons',
     set('''blit masked_blit stretch_blit masked_stretch_blit stretch_sprite draw_sprite
            draw_sprite_v_flip draw_sprite_h_flip draw_sprite_vh_flip draw_trans_sprite
            draw_lit_sprite draw_gouraud_sprite draw_character_ex draw_rle_sprite
            draw_trans_rle_sprite draw_lit_rle_sprite get_rle_sprite destroy_rle_sprite
            rotate_sprite rotate_sprite_v_flip rotate_scaled_sprite rotate_scaled_sprite_v_flip
            pivot_sprite pivot_sprite_v_flip pivot_scaled_sprite pivot_scaled_sprite_v_flip
            quad3d quad3d_f triangle3d triangle3d_f polygon3d polygon3d_f clip3d clip3d_f'''.split()),
     []),

    ('color', 'color', 'palettes, color tables, blenders, text and fonts',
     set('''set_palette set_palette_range get_palette get_palette_range set_color get_color
            select_palette unselect_palette fade_interpolate fade_from_range fade_in_range
            fade_out_range fade_from fade_in fade_out bestfit_color create_rgb_table
            create_trans_table create_light_table create_color_table create_blender_table
            palette_color rgb_map color_map black_palette desktop_palette default_palette
            set_blender_mode set_blender_mode_ex hsv_to_rgb rgb_to_hsv generate_332_palette
            font text_length text_height textout_ex textout_centre_ex textout_right_ex
            textout_justify_ex textprintf_ex textprintf_centre_ex textprintf_right_ex
            textprintf_justify_ex destroy_font allegro_404_char is_color_font is_mono_font
            zcport_font_vtable _rgb_scale_5 _rgb_scale_6'''.split()),
     [r'^(makecol|makeacol)\w*$', r'^get[rgba](8|15|16|24|32|_depth)?$',
      r'^set_\w*_blender$', r'^_blender_', r'^_palette_color\d+$']),

    ('files', 'files', 'packfiles, LZSS, datafiles, file utilities, bitmap saving',
     set('''packfile_password exists file_exists file_size_ex file_time delete_file
            for_each_file_ex al_findfirst al_findnext al_findclose get_extension get_filename
            replace_filename replace_extension append_filename put_backslash fix_filename_case
            fix_filename_slashes fix_filename_path canonicalize_filename make_relative_filename
            make_absolute_filename is_relative_filename load_datafile unload_datafile
            load_datafile_object unload_datafile_object find_datafile_object
            register_datafile_object fixup_datafile register_bitmap_file_type save_bitmap
            save_bmp save_pcx save_tga load_bitmap load_bmp load_pcx loadpng_init jpgalleg_init
            _al_drive_exists _al_getdrive _al_getdcwd'''.split()),
     [r'^pack_', r'^_al_file', r'^lzss_', r'^(create|free)_lzss_']),

    ('ucfg', 'ucfg', 'unicode and config files',
     set('''set_uformat get_uformat uconvert uconvert_ascii uconvert_toascii _ustrdup
            empty_string'''.split()),
     [r'^u(get|set|width|cwidth|is|offset|insert|remove|to|str|v?s?z?n?printf|convert)\w*$',
      r'config']),

    ('platform', 'platform', 'SDL3 display via Vulkan, timers, input, sound stubs',
     set('''set_gfx_mode vsync enable_triple_buffer request_video_bitmap create_video_bitmap
            show_video_bitmap poll_scroll request_scroll scroll_screen is_windowed_mode
            gfx_capabilities gfx_driver win_get_window _gfx_mode_set_count install_timer
            remove_timer install_int install_int_ex remove_int rest rest_callback retrace_count
            install_joystick remove_joystick poll_joystick joy num_joysticks install_sound
            remove_sound reserve_voices play_sample stop_sample adjust_sample destroy_sample
            create_sample set_volume get_volume set_hardware_volume digi_driver midi_driver
            play_midi stop_midi destroy_midi load_midi play_audio_stream stop_audio_stream
            get_audio_stream_buffer free_audio_stream_buffer zcport_sdl_register'''.split()),
     [r'^(allocate|deallocate|reallocate|release)_voice$', r'^voice_', r'^midi_']),

    ('gui', 'gui', 'dialog engine (in-game menus)',
     set('''do_dialog popup_dialog init_dialog update_dialog shutdown_dialog object_message
            broadcast_dialog_message dialog_message find_dialog_focus offer_focus centre_dialog
            position_dialog set_dialog_color active_menu active_dialog do_menu alert alert3
            file_select_ex'''.split()),
     [r'^d_\w+_proc$', r'^gui_']),
]

# ---- notes the report prints next to a symbol ---------------------------------

NOTES = {
    'zcport_sdl_register': 'NEW contract from integration (src/port/include/zcport/entry.h): '
                           'the entry point calls it before the game\'s main(); it must register '
                           'the platform table with zcport_set_platform().',
    'win_get_window': 'Must return SDL\'s HWND (SDL_PROP_WINDOW_WIN32_HWND_POINTER); src/zsys.cpp '
                      'moves the window to the "window_display" screen with it.',
    'destroy_midi': 'Not a stub: the game allocates MIDI objects with _AL_MALLOC (src/qst.cpp '
                    'read_midi) and the datafile loader creates them for zelda.dat; free the '
                    'track data and the struct with _AL_FREE, stop it first if it is playing.',
    'load_datafile': 'Creates objects other modules own: FONT (color, zcport/font_internal.h), '
                     'SAMPLE (platform, sfx.dat), MIDI (platform, zelda.dat), BITMAP; '
                     'unload_datafile needs their destroyers.',
    '_color_load_depth': 'Allegro internal used by the game\'s GIF loader (src/load_gif.cpp, '
                         'added to zplayer by integration). Declared in allegro/internal/aintern.h; '
                         'depends on _color_depth and get_color_conversion() (foundation, '
                         'src/port/al/color_conversion.cpp).',
    'select_palette': 'Used by the game\'s GIF loader (src/load_gif.cpp), added to zplayer by '
                      'integration; not in the API inventory.',
    'unselect_palette': 'Used by the game\'s GIF loader (src/load_gif.cpp), added to zplayer by '
                        'integration; not in the API inventory.',
    'loadpng_init': 'Registers the "png" type for save_bitmap() (screenshots); no libpng.',
    'jpgalleg_init': 'Registers the "jpg" type for save_bitmap() (screenshots).',
    'is_windowed_mode': 'An inline in Allegro (gfx.inl), a real function in zcport.',
    'midi_driver': 'Variable. src/zc_sys.cpp jukebox() does strcmp(midi_driver->name, "DIGMID"), '
                   'so it must point to a driver with a name even while MIDI is silent.',
    'install_int_ex': 'Also used by src/port/game/zcmusic_port.cpp (25 ms music poll, after '
                      'install_timer()).',
    'remove_int': 'Also used by src/port/game/zcmusic_port.cpp (zcmusic_exit()).',
    'font': 'Variable (FONT *).',
    'black_palette': 'Variable (PALETTE).',
    'palette_color': 'Variable (int *).',
    'rgb_map': 'Variable (RGB_MAP *), the game sets it.',
    'joy': 'Variable (JOYSTICK_INFO array); the game reads stick/axis/button fields directly.',
    'num_joysticks': 'Variable.',
    'midi_pos': 'Variable; the Triforce and title code wait on it.',
    'midi_loop_start': 'Variable.',
    'midi_loop_end': 'Variable.',
    'active_menu': 'Variable (MENU *).',
    'gui_mouse_focus': 'Variable.',
    'gui_fg_color': 'Variable.',
    'gui_mg_color': 'Variable.',
    'gui_bg_color': 'Variable.',
    'gui_font_baseline': 'Variable.',
}

for _name in ('gui_mouse_x', 'gui_mouse_y', 'gui_mouse_z', 'gui_mouse_b'):
    NOTES[_name] = 'Function pointer variable (AL_FUNCPTR); the default reads the mouse state (foundation).'
for _name in ('ugetc', 'ugetx', 'ugetxc', 'usetc', 'uwidth', 'ucwidth', 'uisok'):
    NOTES[_name] = 'Function pointer variable (AL_FUNCPTR), switched by set_uformat().'

# Symbols that no module's scope covers. The report lists them as gaps
# even though a module is suggested.
UNOWNED_OVERRIDE = {
    '_color_load_depth': 'files (next to the datafile bitmap loader) or foundation '
                         '(next to set_color_conversion)',
}

GUI_HAS_NO_AGENT = True

LINK_RE = re.compile(r'^\s*(\S+?)\.obj : error LNK20(?:19|01): unresolved external symbol (.+?)'
                     r'(?: referenced in function .*)? \[')


def parse(log_path):
    symbols = collections.OrderedDict()
    with open(log_path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = LINK_RE.match(line)
            if not m:
                continue
            obj, sym = m.group(1), m.group(2).strip()
            if sym.startswith('"'):
                mm = re.match(r'"(.*)" \((.*)\)$', sym)
                name = mm.group(1) if mm else sym
            else:
                name = sym[1:] if sym.startswith('_') else sym
                name = re.sub(r'@\d+$', '', name)
            symbols.setdefault(name, set()).add(obj)
    return symbols


def owner(name):
    for key, _title, _scope, names, patterns in MODULES:
        if name in names or any(re.search(p, name) for p in patterns):
            return key
    return None


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    symbols = parse(argv[1])
    groups = collections.OrderedDict((m[0], []) for m in MODULES)
    unowned = []
    for name in sorted(symbols, key=lambda s: s.lower()):
        key = owner(name)
        if key is None or name in UNOWNED_OVERRIDE:
            unowned.append(name)
        if key is not None:
            groups[key].append(name)

    def refs(name):
        return ', '.join(sorted(symbols[name], key=str.lower))

    out = []
    w = out.append
    w('# zplayer link gaps')
    w('')
    w('Generated by `src/port/tools/zplayer_link_gaps.py` from the link of the `zplayer`')
    w('target in a tree that has only the committed zcport parts (foundation, fixmath,')
    w('primitives in progress) and the integration work. Every symbol below is referenced')
    w('by the player (or by the integration files) and defined nowhere yet. Re-run the')
    w('script on a fresh link log after merging modules:')
    w('')
    w('```')
    w('cmake --build build-wt --config Release --target zplayer -- -m -v:minimal -nologo > link.log 2>&1')
    w('python -I src/port/tools/zplayer_link_gaps.py link.log docs/zplayer-link-gaps.md')
    w('```')
    w('')
    w('"Referenced from" names the object files (game sources) that use the symbol.')
    w('Symbols that the modules need from each other are not visible here; they show up')
    w('in the first link after the merge.')
    w('')
    w('## Summary')
    w('')
    w('| module | scope | missing |')
    w('|---|---|---:|')
    for key, title, scope, _n, _p in MODULES:
        w('| %s | %s | %d |' % (title, scope, len(groups[key])))
    w('| **no module** | gaps to assign | %d |' % len(unowned))
    w('| **total** | | %d |' % len(symbols))
    w('')
    w('## Gaps: symbols no module owns')
    w('')
    if unowned:
        w('| symbol | referenced from | suggested owner | note |')
        w('|---|---|---|---|')
        for name in unowned:
            w('| `%s` | %s | %s | %s |' % (name, refs(name), UNOWNED_OVERRIDE.get(name, '?'),
                                        NOTES.get(name, '')))
    else:
        w('None.')
    w('')
    if GUI_HAS_NO_AGENT and groups['gui']:
        w('**The gui module (dialog engine) has no agent working on it**: its %d symbols'
          % len(groups['gui']))
        w('below are owned by the module scope but nobody is implementing them yet.')
        w('')
    for key, title, scope, _n, _p in MODULES:
        names = groups[key]
        w('## %s (%d)' % (title, len(names)))
        w('')
        w('Scope: %s.' % scope)
        w('')
        if not names:
            w('Nothing missing.')
            w('')
            continue
        w('| symbol | referenced from | note |')
        w('|---|---|---|')
        for name in names:
            w('| `%s` | %s | %s |' % (name, refs(name), NOTES.get(name, '')))
        w('')
    with open(argv[2], 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out))
    sys.stdout.write('%d symbols, %d without owner -> %s\n' % (len(symbols), len(unowned), argv[2]))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

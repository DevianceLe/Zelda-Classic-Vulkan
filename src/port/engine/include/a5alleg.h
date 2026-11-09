// The bridge calls the engine source makes between its software bitmaps and the
// display, written from scratch for zcport.
#ifndef ZC_A5ALLEG_H
#define ZC_A5ALLEG_H

#include <allegro.h>
#include "allegro5/zc_a5_api.h"

ALLEGRO_DISPLAY* all_get_display();
void all_toggle_fullscreen(bool fullscreen);
int all_joystick_generation();
ALLEGRO_BITMAP* all_get_a5_bitmap(BITMAP* bp);
void all_render_a5_bitmap(BITMAP* bp, ALLEGRO_BITMAP* a5bp);
void all_disable_threaded_display();
void all_process_display_events();
bool all_get_fullscreen_flag();
void all_set_resize_flag(bool resize);
void all_set_display_flags(int flags);
int all_get_display_flags();
void all_set_transparent_palette_index(int index);
uint32_t all_get_render_generation();

// Direct line access on zcport's memory bitmaps, where every line is plain memory.
inline uintptr_t bmp_read_line(BITMAP* bmp, int y) { return (uintptr_t)bmp->line[y]; }
inline uintptr_t bmp_write_line(BITMAP* bmp, int y) { return (uintptr_t)bmp->line[y]; }
inline void bmp_unwrite_line(BITMAP*) {}
inline uint8_t bmp_read8(uintptr_t addr) { return *(uint8_t*)addr; }
inline void bmp_write8(uintptr_t addr, int c) { *(uint8_t*)addr = (uint8_t)c; }

// zcport has a single key-state array; the engine writes it under both names.
#define _key key
extern "C" const char* const* const _keyboard_common_names;

#endif

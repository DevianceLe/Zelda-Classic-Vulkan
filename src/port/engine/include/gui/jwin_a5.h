// Color helpers the engine source calls, written from scratch for zcport. The
// implementations live in src/port/engine/a5/.
#ifndef ZC_ENGINE_JWIN_A5_H
#define ZC_ENGINE_JWIN_A5_H

#include "gui/jwin.h"
#include "allegro5/zc_a5_api.h"

extern ALLEGRO_COLOR jwin_a5_colors[9];
ALLEGRO_COLOR jwin_a5_pal(int jc);
void jwin_set_a5_colors(ALLEGRO_COLOR* colors);

ALLEGRO_COLOR a5color(RGB c);
ALLEGRO_COLOR a5color(int index);

int32_t jwin_win_proc_a5(int32_t msg, DIALOG* d, int32_t c);

void al_draw_int_rectangle(int x1, int y1, int x2, int y2, ALLEGRO_COLOR col, double width);

#endif

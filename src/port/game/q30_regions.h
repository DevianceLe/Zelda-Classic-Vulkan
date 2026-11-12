// 3.0 scrolling regions (upstream's "z3 scrolling"), player only.
//
// A region is a rectangle of screens on a map with the same nonzero region id (MAP section
// v31+, q255_map_region_ids). Upstream keeps every sprite in region-wide coordinates; this
// engine keeps its game logic per screen (tmpscr is the screen Link stands on, positions are
// local to it) and adds:
//   - a camera in region coordinates that follows Link (upstream's calculate_viewport),
//   - drawing of the other screens of the region that the camera shows,
//   - a seamless change of screen when Link's centre crosses a border inside the region
//     (no scroll; enemies, items and enemy weapons of the old screen go, as on a scroll),
//   - collision that looks into the neighbouring screen near a border inside the region.
// Scripts see region-wide values through Region->* and Viewport->*.
// Nothing here runs for 2.53 quests or screens outside regions.

#ifndef _Q30_REGIONS_H_
#define _Q30_REGIONS_H_

#include "zc_alleg.h"

struct mapscr;

bool q30_region_on();               // the current screen is in a scrolling region
void q30_region_screen_loaded();    // after loadscr(0): the region of currscr
void q30_region_forget();           // a quest was loaded or a game ended

int q30_region_origin();            // the region's top-left screen (currscr when not in one)
int q30_region_id();                // its id (1-9; 0 when not in one)
int q30_region_width();             // in pixels (256 when not in one)
int q30_region_height();            // in pixels (176)
int q30_region_screens_wide();
int q30_region_screens_high();
int q30_region_offset_x();          // the current screen's top-left in region pixels
int q30_region_offset_y();

// The camera for Link at local (lx, ly): its top-left relative to the current screen's top-left
// (both 0 outside regions), and in region pixels (Viewport->X / Y).
void q30_region_camera(int lx, int ly, int *cam_x, int *cam_y);
void q30_region_viewport(int *vx, int *vy);

// draw_screen: compose the layers below the sprites (scrollbuf, playfield at y = pfo) with the
// other screens the camera shows, then window them through the camera.
void q30_region_compose_lower(BITMAP *scrollbuf);
// draw_screen: a layer above the sprites (do_layer types 2-5, -1, -4) of the current screen and
// of the other screens in view, through the camera.
void q30_region_upper_layer(BITMAP *bmp, int type, mapscr *this_screen, bool drawprimitives);

// The offset sprites and script drawings get while draw_screen draws above the lower layers
// (both 0 when no camera is in use).
extern int q30_draw_cam_x, q30_draw_cam_y;
void q30_region_begin_sprites();    // sets them from the camera
void q30_region_end_sprites();      // back to 0

// _walkflag for a point (or 2 points, cnt 2) outside the current screen: -1 when not in a region
// or the points are all on the current screen, else 1 solid / 0 walkable (the neighbour's combos).
int q30_region_walkflag(int x, int y, int cnt);

// Link at local (x, y): the direction of a border inside the region his centre has crossed
// (-1 none), and the screen step in ndx / ndy. q30_region_has_neighbour(dir): a screen of the
// region lies that way (up/down/left/right), so checkscroll doesn't scroll there.
int q30_region_crossed(int x, int y, int *ndx, int *ndy);
bool q30_region_has_neighbour(int dir);
void q30_region_keep_current();     // before the hand-off: keep the screen Link leaves for display

// Sprites live on across the region (upstream keeps them in region-wide coordinates). In the
// current screen's coordinates: the region's rectangle, and the camera's window (both
// 0, 0, 256, 176 outside regions).
void q30_region_bounds(int *l, int *t, int *r, int *b);
void q30_region_view_bounds(int *l, int *t, int *r, int *b);

// The screen each enemy, item and enemy weapon came from (upstream's screen_spawned), so a
// screen's "enemies killed" logic counts only its own: sprites not yet tagged take the current
// screen. -1: not in a region, or not tagged.
void q30_region_tag_sprites();
int q30_region_sprite_screen(long uid);

// A screen's own enemy state across hand-offs (2.53 keeps it for the current screen only):
// whether its guys and enemies were loaded, its item state (hasitem) and did_secret.
void q30_region_save_screen_state();
bool q30_region_restore_screen_state();     // false: a screen not visited yet in this region

// Each frame: the other screens of the region the camera shows load their enemies (once a stay).
void q30_region_load_enemies_in_view();

#endif

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
// After loadscr(0): the region of currscr. destdmap is the DMap loadscr loaded it for (-1: the
// current one); during a scroll into another DMap currdmap is still the old one.
void q30_region_screen_loaded(int destdmap = -1);
void q30_region_forget();           // a quest was loaded or a game ended

int q30_region_origin();            // the region's top-left screen (currscr when not in one)
int q30_region_id();                // its id (1-9; 0 when not in one)
int q30_region_width();             // in pixels (256 when not in one)
int q30_region_height();            // in pixels (176)
bool q30_extended_height_mode();    // upstream's extended viewport: a 232-row view, playing field offset 0
int q30_view_h();                   // the camera's height: 232 in extended height mode, else 176
void q30_apply_field_offset();      // playing_field_offset 0 or 56 for the mode (not while scrolling)
int q30_region_screens_wide();
int q30_region_screens_high();
int q30_region_offset_x();          // the current screen's top-left in region pixels
int q30_region_offset_y();

// ---- what scripts see in a scrolling region ----------------------------------------------------
//
// Upstream keeps every sprite of a scrolling region in region-wide coordinates, so its scripts
// read and write positions in them (Hero->X/Y, npc / lweapon / eweapon / item X/Y, combo
// positions, Screen->DrawOrigin REGION). This engine keeps them local to the current screen, so
// the script registers convert: a read adds the current screen's offset in the region, a write
// takes it off. Outside a scrolling region (and in every 2.53 quest) the offset is 0.
//
// The current screen's pixel offset from its region's top-left screen; 0 outside a region.
int q30_region_origin_x();
int q30_region_origin_y();

// Combo positions across the region (upstream's rpos): the screen's index in the region, row by
// row, times 176, plus the position on that screen. Outside a region they are 0..175 as before.
int q30_region_num_rpos();                      // 176 times the region's screens
int q30_region_rpos_at(int x, int y);           // the combo at region pixel (x, y), clamped into the region (ComboAt)
void q30_region_rpos_xy(int rpos, int *x, int *y);  // that combo's top-left in region pixels
int q30_region_rpos_of_pos(int pos);            // a position on the current screen as an rpos
// The screen an rpos is on, as layer `layer` (0 the screen, 1-6 its layers): tmpscr / tmpscr2 for
// the current screen, else the region's copy of the other screen (NULL for a layer it doesn't
// use); *pos gets the position on it. NULL also for an rpos past the region. *current says
// whether it is the current screen (whose combos the 2.53 code around them may also act on).
mapscr *q30_region_rpos_screen(int rpos, int layer, int *pos, bool *current);
// The region screen a point in current-screen pixels is on (currscr outside a region).
int q30_region_screen_at(int lx, int ly);

// The camera: its top-left relative to the current screen's top-left (both 0 outside regions;
// lx, ly are unused, the camera follows Link by itself), and in region pixels (Viewport->X / Y;
// during a scroll, the scroll's view in the new region's pixels).
void q30_region_camera(int lx, int ly, int *cam_x, int *cam_y);
// The camera is used: in a scrolling region, or on a lone screen while a script places the
// viewport (Viewport->Mode 2: upstream's 1x1 region; Modes 0 and 1 keep it at 0, 0 there).
bool q30_region_camera_used();
void q30_region_viewport(int *vx, int *vy);
// Viewport-> for scripts (upstream's scripting/types/viewport.cpp). Mode: 0 centred on the target
// and kept in the region, 1 centred, 2 placed by the script (X / Y); a screen load or a scroll
// puts mode 0 and the Hero back. Width / Height: what the camera last set (256, 176 or 232), or
// a script since. Target: the followed sprite's UID (the Hero's 1). The follow settings in
// effect (Q30_VP_*; distances in 1/10000 pixels as scripts see them, speeds as stored) and a
// script's overrides of them (range checked by the caller), cleared by ResetFollowSettings.
enum { Q30_VP_DEADZONE_W, Q30_VP_DEADZONE_H, Q30_VP_LOOKAHEAD_X, Q30_VP_LOOKAHEAD_Y,
       Q30_VP_LOOKAHEAD_SPEED, Q30_VP_RECENTER_SPEED, Q30_VP_RECENTER_DELAY, Q30_VP_FOLLOW_COUNT };
int q30_viewport_mode();
void q30_viewport_set_mode(int mode);
void q30_viewport_set_xy(bool y, int v);
int q30_viewport_w();
int q30_viewport_h();
void q30_viewport_set_size(bool h, int v);
long q30_viewport_target();
void q30_viewport_set_target(long uid);
long q30_viewport_follow(int which);
void q30_viewport_set_follow(int which, long v);
void q30_viewport_reset_follow_settings();
// dmapdata->Viewport*: a DMap's own follow settings (distances in pixels, speeds as stored)
long q30_dmap_viewport_follow(int dmap, int which);
void q30_set_dmap_viewport_follow(int dmap, int which, long v);
// Once a frame from the game loop, after Link moved: the camera follows him (upstream's
// tick_viewport_follow: dead zone, lookahead and recentering, from init data or the DMap).
// Nothing for 2.53 quests.
void q30_region_tick_camera();

// draw_screen's layers below the sprites. With the camera off this screen's grid they are drawn
// on a canvas of the screens around this one: q30_region_lower_begin returns it (else scrollbuf)
// and sets q30_draw_cam_x/y so that drawing at (x - q30_draw_cam_x, y + pfo - q30_draw_cam_y)
// places a point (x, y) of this screen on it (both 0 for scrollbuf), so script drawings on these
// layers land anywhere in view, uncut. After each of this screen's lower layers,
// q30_region_lower_layer draws that layer of the other screens in view, and q30_region_lower_end
// puts the camera's window of the canvas into scrollbuf's playing field.
enum { Q30_LOWER_BG2, Q30_LOWER_BG3, Q30_LOWER_LAYER0, Q30_LOWER_LAYER1, Q30_LOWER_FFCS, Q30_LOWER_LAYER2 };
BITMAP *q30_region_lower_begin(BITMAP *scrollbuf);
void q30_region_lower_layer(BITMAP *lower, int which);
void q30_region_lower_end(BITMAP *scrollbuf);
// For drawing code with no offsets of its own (lens hints): the canvas seen from this screen's
// top-left (a sub-bitmap, so (x, y + pfo) lands on this screen's (x, y)); else lower itself.
BITMAP *q30_region_lower_local(BITMAP *lower);
// Where the view's top-left (the screen's top-left, above the playing field) is on bmp: on that
// canvas, the camera's window; 0, 0 for any other bitmap.
void q30_draw_view_origin(BITMAP *bmp, int *x, int *y);
// draw_screen: a layer above the sprites (do_layer types 2-5, -1, -4) of the current screen and
// of the other screens in view, through the camera.
void q30_region_upper_layer(BITMAP *bmp, int type, mapscr *this_screen, bool drawprimitives);
void q30_region_overhead_layers(BITMAP *bmp);  // 3.0 qr_OVERHEAD_COMBOS_L1_L2: layers 1-2 overhead combos

// The screens the camera shows, for what is drawn over them all (dark rooms): the current screen
// first (tmpscr and its layers, live), then the region's other screens in view, or within extra
// pixels of the view (upstream lights a dark room from torches up to 128 pixels outside it).
// ox, oy: each screen's top-left relative to the current screen's. Outside regions (and while a
// scroll runs) only the current screen. Returns how many (at most Q30_VIEW_SCREENS).
struct Q30ViewScreen
{
    int ox, oy;
    const mapscr *base;
    const mapscr *layers;       // its 6 layers (valid 0: none)
};
enum { Q30_VIEW_SCREENS = 25 };
int q30_region_view_screens(Q30ViewScreen *out, int extra);

// The offset sprites and script drawings get while draw_screen draws above the lower layers
// (both 0 when no camera is in use).
extern int q30_draw_cam_x, q30_draw_cam_y;
void q30_region_begin_sprites();    // sets them from the camera
void q30_region_end_sprites();      // back to 0

// A 2.55/3.0 script's screen drawing (do_primitives): where it goes for its Screen->DrawOrigin
// (upstream's get_draw_origin_offset). (xoff, yoff) is what the drawing call passes for the
// playing field's top-left, less the camera when do_layer moved it (q30_draw_cam_x/y), or on the
// lower layers' canvas; *x / *y get the command's offsets. False: its origin sprite is gone, so
// it isn't drawn.
bool q30_draw_origin_offset(int origin, int target_uid, int xoff, int yoff, int *x, int *y);

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
// What an enemy adds while it animates (the enemies it splits into or summons) belongs to its own
// screen, as upstream's addenemy(screen_spawned, ...): q30_region_tag_children tags what the last
// enemy animated added (q30_region_spawn.cpp), q30_region_sprite_set_screen tags one sprite.
void q30_region_tag_children();
void q30_region_sprite_set_screen(long uid, int scr);
// For an enemy that places itself by "the screen" (a Zora's water, a Wizzrobe's teleport, the
// layers a summoner draws from): the screen it came from and that screen's top-left in the
// current screen's pixels. tmpscr and 0, 0 for the current screen's own, outside regions and in
// every 2.53 quest.
mapscr *q30_region_sprite_home(long uid, int *ox, int *oy);

// A screen's own enemy state across hand-offs (2.53 keeps it for the current screen only):
// whether its guys and enemies were loaded, its item state (hasitem) and did_secret.
void q30_region_save_screen_state();
bool q30_region_restore_screen_state();     // false: a screen not visited yet in this region

// Each frame: the screens of the region the camera shows load their enemies (once a stay).
// False: not in a region with live screens (the caller loads the current screen's as before).
bool q30_region_load_enemies_in_view();

// ---- every screen's guys, items and enemies (q30_regions.cpp, q30_region_spawn.cpp) ---------
//
// Upstream's loadguys runs once for every screen of a region, on the frame after it loads: each
// screen's guy, fires, item and room sprites come out where they are, whatever screen Link is on.
// Its loadenemies runs every frame for every screen whose enemies aren't out yet: they come out
// once the viewport touches the screen (a side-loading pattern walks them in from then on). And
// the special room (a shop, a secret money room...) set up when a guy has faded in is always the
// region's top-left screen's.
// False: not in a region with live screens (the caller loads the current screen as before).
bool q30_region_loadguys();
bool q30_region_setupscreen();
// Each frame: the other screens whose enemies are all gone get what follows (their item, their
// secrets), once.
void q30_region_enemies_gone();

// Falling rocks (upstream's eRock / eBoulder): the left edge of the screen the sprite came from
// and the viewport's top and bottom, in the current screen's coordinates (0, 0, 176 outside regions).
int q30_region_rock_x(long uid);
int q30_region_rock_top();
int q30_region_rock_bottom();

// Sprites away from the camera (upstream's viewport suspend and despawn ranges): what the quest
// file gives an enemy or an item, and whether the last animate left a sprite of that list
// suspended (its script doesn't run either).
class sprite_list;
void q30_vp_ranges_reset();
void q30_vp_guy_ranges(int id, long suspend, long despawn);
void q30_vp_item_ranges(int id, long suspend, long despawn);
bool q30_vp_frozen(sprite_list *list, long uid);
// enemy::Dead, at the end of the death sequence: a despawn range removed this enemy (asked once)
bool q30_vp_despawned(long uid);
// Weapons: what an enemy's or an item's weapon data gives the weapons it fires, a weapon fired with
// other weapon data (a combo's), and weapon::clip's test (false: the weapon has no despawn range).
class sprite;
void q30_vp_note(const char *what, int id, long suspend, long despawn);    // ZC_VP_RANGELOG
void q30_vp_guy_weapon_ranges(int id, long suspend, long despawn);
void q30_vp_item_weapon_ranges(int id, long suspend, long despawn);
void q30_vp_item_weapon_get(int id, long *suspend, long *despawn);
void q30_vp_weapon_ranges(long uid, long suspend, long despawn);
bool q30_vp_weapon_despawn(sprite *w, bool *gone);
// A weapon with these ranges, tile area and hit box (current-screen pixels) would be suspended or
// removed as soon as it is out (upstream's shooter combos don't fire it then).
bool q30_vp_shot_held(long suspend, long despawn, int tx, int ty, int tw, int th, int hx, int hy, int hw, int hh);
// FFC i of the current screen is past its viewport suspend (or despawn) range.
bool q30_vp_ffc_beyond(int i, bool despawn);

// test switches: ZC_TEST_KEYS (the controls by game frame), ZC_REGION_SPRLOG (a sprite trace)
void q30_test_keys(bool *control_state);
void q30_region_sprlog();

// ---- every screen of a region live: FFCs and scripts (q30_regions.cpp) ----------------
//
// Upstream loads all the screens of a scrolling region when Link enters it, and runs all their
// FFCs (movement, scripts, changers, solidity, drawing) and their screen and combo scripts every
// frame; walking over a border inside the region restarts nothing. This engine has one live
// screen (tmpscr, tmpscr2, ffcScriptData and the script engine's FFC, screen and combo script
// state, the trigger state). So each other screen of the region gets a slot of its own, loaded
// as loadscr loads a screen when Link enters the region, which keeps that state while Link is
// elsewhere in the region:
//   - q30_region_each_screen: each frame, every screen of the region runs a part of the frame
//     (its screen script, its FFC scripts, its FFCs' movement, its combos' cycling and trigger
//     timers, its combo scripts, Waitdraw halves), row by row from the top-left as upstream does.
//     A screen that isn't the current one stands in for it while it runs: its slot and the
//     globals change places, currscr is it, and Link and the sprites are moved into its
//     coordinates, so scripts see what they see on it.
//   - the hand-off (LinkClass::q30_region_handoff): the next screen's slot comes in and the
//     current screen goes to a slot; nothing is loaded again (q30_region_go_live).
// Every FFC of the region is drawn where it is; solid ones stop Link and the combo types of the
// others act where they are (MAPFFCOMBO). Nothing here runs for 2.53 quests or outside regions.

// what a screen of the region runs in q30_region_each_screen
enum
{
    Q30_EACH_SCREEN_SCRIPT, Q30_EACH_FFC_SCRIPTS, Q30_EACH_FFC_PRELOAD, Q30_EACH_FFC_MOVE,
    Q30_EACH_COMBO_SCRIPTS, Q30_EACH_FFC_WAITDRAW, Q30_EACH_COMBO_WAITDRAW,
    Q30_EACH_COMBO_CYCLE    // combos cycling, and (another screen) its combo triggers' timers
};

// Runs fn(arg) for every screen of the region that has something to do for `what` (the current
// screen always), each standing in for the current one in turn. False when not in a region with
// live screens: the caller runs fn itself, for the current screen, as before.
bool q30_region_each_screen(int what, void (*fn)(int), int arg);

// While a screen of the region loads beside the current one (loadscr: no screen-change events,
// no FFC arrays freed, its own trigger setup).
bool q30_region_loading_other();

// The screen Link is on (upstream's hero_scr), for what a script does to Link through his
// screen: Hero->Warp, PitWarp and WarpEx write its side warp A. While another screen of the
// region stands in for the current one to run its scripts, tmpscr is that screen and Link's
// waits in a slot: this is the slot's. Any other time (and always in 2.53 quests) it is tmpscr.
mapscr *q30_region_hero_scr();

// The hand-off to region screen `scr` when the region's screens are live: its slot becomes the
// current screen (currscr, tmpscr, its FFCs and scripts as they are), the current screen goes to
// a slot. Link and the sprites aren't moved. False: no live slot for it (load it as before).
bool q30_region_go_live(int scr);

// FFCs across the region, for scripts (q30_scripts_region.h). An FFC's ID there is upstream's:
// (its screen's index in the region, row by row) * 128 + its slot + 1; its X and Y are region
// pixels. Outside a region every screen is the current one at index 0.
int q30_region_screen_index(int scr);       // -1: not a screen of the region
int q30_region_index_screen(int index);     // -1: no such screen in the region
void q30_region_screen_pixels(int scr, int *x, int *y);    // its top-left in region pixels
mapscr *q30_region_ffc_table(int scr);      // its FFCs: tmpscr for the current one; NULL if none
void *q30_region_screen_scripts(int scr);   // its slot's script state (NULL for the current one)
// FFCs as upstream's sprites (2.55/3.0 quests): slot i of screen m's sprite uid, given from the
// sprite uid counter the first time it is drawn or named (0 for no such slot); and the live FFC
// (the current screen's, or a screen's of the region) a uid names: false for none.
long q30_ffc_uid(mapscr *m, int i);
bool q30_ffc_by_uid(long uid, int *scr, int *index);

// Solid FFCs of the region's other screens over the points walkflag tests (cnt of them, 8 apart).
bool q30_region_ffc_solid_at(int x, int y, int cnt);
// MAPFFCOMBO: the combo of an FFC of the region's other screens covering a point of the current
// screen (as ffcIsAt tests it), so their combo types hurt, block weapons and so on there; 0: none.
int q30_region_ffcombo_at(int x, int y);

// ---- scrolling between screens (LinkClass::scrollscr) in a 2.55 / 3.0 quest -----------------
//
// Game->Scrolling[] (upstream's FFCore.ScrollingData, zc/ffscript.h): what a scroll is doing, for
// scripts. Every entry is 0 and DIR is -1 while no scroll runs. Positions are in the old region's
// pixels; NX/NY/OX/OY/NRX/NRY/ORX/ORY are relative to the scroll's view, which moves each frame.
enum
{
    Q30_SCROLL_DIR,
    Q30_SCROLL_NX, Q30_SCROLL_NY, Q30_SCROLL_OX, Q30_SCROLL_OY,
    Q30_SCROLL_NRX, Q30_SCROLL_NRY, Q30_SCROLL_ORX, Q30_SCROLL_ORY,
    Q30_SCROLL_NEW_REGION_DELTA_X, Q30_SCROLL_NEW_REGION_DELTA_Y,
    Q30_SCROLL_NEW_SCREEN_X, Q30_SCROLL_NEW_SCREEN_Y, Q30_SCROLL_OLD_SCREEN_X, Q30_SCROLL_OLD_SCREEN_Y,
    Q30_SCROLL_NEW_HERO_X, Q30_SCROLL_NEW_HERO_Y, Q30_SCROLL_OLD_HERO_X, Q30_SCROLL_OLD_HERO_Y,
    Q30_SCROLL_NEW_REGION_SCREEN_WIDTH, Q30_SCROLL_NEW_REGION_SCREEN_HEIGHT,
    Q30_SCROLL_OLD_REGION_SCREEN_WIDTH, Q30_SCROLL_OLD_REGION_SCREEN_HEIGHT,
    Q30_SCROLL_NEW_VIEWPORT_WIDTH, Q30_SCROLL_NEW_VIEWPORT_HEIGHT,
    Q30_SCROLL_OLD_VIEWPORT_WIDTH, Q30_SCROLL_OLD_VIEWPORT_HEIGHT,
    Q30_SCROLL_NEW_VIEWPORT_X, Q30_SCROLL_NEW_VIEWPORT_Y,
    Q30_SCROLL_OLD_VIEWPORT_X, Q30_SCROLL_OLD_VIEWPORT_Y,
    Q30_SCROLL_COUNT
};

int q30_scroll_data(int index);     // Game->Scrolling[index] (0 out of range)
void q30_scroll_reset();            // no scroll: a new or continued game, or a scroll that ended

// The scroll itself. 2.53's scroll draws the old and the new screen side by side and moves the
// view from one to the other. In a 3.0 quest the view also keeps the old region's camera on the
// other axis and moves it, 4 pixels a frame, to where the new region's camera will be (upstream's
// secondary_axis_alignment_amount), before the scroll when the old region can show that, after it
// otherwise; the screens beside the old and the new screen on that axis are drawn as it shows them.
// All of these do nothing for 2.53 quests.
void q30_scroll_begin(int dir);     // after the frames before the scroll, before the new screen loads
void q30_scroll_loaded(bool moves); // the new screen is loaded (moves: the scroll is animated)
bool q30_scroll_on();               // a 3.0 quest's scroll is running
// Each frame of the scroll loop: aligning first (the scroll waits); the alignment step, which says
// bit 1: the scroll waits a frame, bit 2: aligning after the scroll (nothing moves); and
// whether any alignment is still to come.
bool q30_scroll_align_first();
int q30_scroll_align_step(bool scroll_left);
bool q30_scroll_align_pending();
// the playing field offset has yet to change (upstream's pfo_counter): no still frames at the end
bool q30_scroll_pfo_pending();
bool q30_scroll_aligning();      // the view still lines up on the other axis (upstream's align_counter)
// The view's offset on the other axis, relative to both screens (add it to the scroll's x or y).
void q30_scroll_offset(int *ox, int *oy);
int q30_scroll_pfo_shift();     // the playing field offset's change so far in a 3.0 scroll (upstream's pfo_counter)
// up/down 3.0 scrolls into or out of a tall view: the extra moves (upstream's longer scroll_amount),
// Link's bounds on the view (scrollscr's y; false: 2.53's 0..160), and whether he ends on the new
// screen's edge (upstream's new_hero_y)
int q30_scroll_extra_steps(int step);
bool q30_scroll_link_bounds(int *lo, int *hi);
bool q30_scroll_link_edge(int dir);
// The view at scroll position (sx, sy) (scrollscr's numbers): Game->Scrolling's moving entries
// and Viewport->X / Y for scripts.
void q30_scroll_view(int sx, int sy);
// Drawing: the lower layers (scrollbuf, built as 2.53 does) through the view onto framebuf, with
// the screens beside the old and the new one; returns false when 2.53's blit does it. And one
// do_layer of those screens, at the offsets the old (tempscreen 3) or new (2) screen is drawn at.
bool q30_scroll_blit_lower(BITMAP *scrollbuf, BITMAP *framebuf, int sx, int sy);
void q30_scroll_layer(BITMAP *bmp, int type, bool new_screen, int x, int y, bool scrolling);
// What a scroll's view shows at (sx, sy), for dark rooms: the old screen (tmpscr[1], tmpscr3), the
// new one (tmpscr, tmpscr2) and the screens beside them the view leans over, each with its
// top-left on the view; *vx, *vy the view's top-left from the new screen's, *lx, *ly Link's on the
// view. 0 when no 2.55/3.0 scroll runs.
int q30_scroll_view_screens(Q30ViewScreen *out, int sx, int sy, int *vx, int *vy, int *lx, int *ly);
void q30_scroll_end();              // the scroll ended: the new region's camera, no Scrolling data

#endif

//--------------------------------------------------------
//  Zelda Classic
//
//  replay.h
//
//  Deterministic input recording and playback, and the
//  per-frame hash log used to compare the Allegro build
//  with zplayer. Opt-in through command-line switches
//  (see docs/zplayer-replay.md); without them every hook
//  below does nothing.
//
//  The hooks touch only game-level state and the Allegro API
//  subset the player already uses, so they behave the same in
//  the Allegro build and in zplayer.
//--------------------------------------------------------

#ifndef _ZC_REPLAY_H_
#define _ZC_REPLAY_H_

struct BITMAP;

// Parses the replay switches and starts recording or playback.
// Call once from main(), after the keyboard, mouse and joystick are
// installed and before the first frame. Exits the program (status 2)
// on a bad switch or an unreadable/invalid file.
void replay_init(int argc, char *argv[]);

// True while recording or playing back (-record / -replay). The game
// then runs in deterministic mode: no sound or enhanced music, no AG
// logo, no screen saver.
bool replay_active();

// -unthrottled: advanceframe() skips the frame-rate throttle.
bool replay_unthrottled();

// The value init_game() passes to srand(). Returns live_seed unless
// recording (records it) or playing back (returns the recorded one).
unsigned int replay_seed(unsigned int live_seed);

// updatescr(): the 256x224 bitmap about to be shown this frame.
void replay_capture_frame(BITMAP *frame);

// advanceframe(): end of a shown frame (after the throttle). Writes the
// frame's hash, stops after -frames N, and sets up the input the game
// reads during the next frame.
void replay_frame_boundary();

// syskeys(): right after poll_joystick().
void replay_joystick_polled();

#endif

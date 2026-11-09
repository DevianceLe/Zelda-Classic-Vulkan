//--------------------------------------------------------
//  Zelda Classic
//
//  replay.cpp
//
//  Deterministic input recording/playback and the per-frame
//  hash log (docs/zplayer-replay.md).
//
//  A "replay frame" is one pass through advanceframe(): the game
//  computes the frame, updatescr() shows it, the throttle waits, and
//  replay_frame_boundary() runs. The input the game reads while it
//  computes frame N (key[], the keyboard buffer, the gui_mouse_*()
//  values, the joystick after syskeys() polls it) is fixed at the
//  boundary before frame N, so a recording replays exactly, at any
//  speed, in the Allegro build and in zplayer.
//--------------------------------------------------------

#include "precompiled.h" //always first

#include <stdint.h> // int64_t / uint64_t
#include <inttypes.h> // PRIx64
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <string>
#include <vector>
#include <set>
#include <atomic>
#include <chrono>

#include "zc_alleg.h"
#include "zdefs.h"
#include "zelda.h"
#include "zc_sys.h"
#include "replay.h"

extern byte epilepsyFlashReduction;

namespace
{

enum Mode { MODE_OFF, MODE_RECORD, MODE_PLAYBACK };

enum
{
    EXIT_REPLAY_OK = 0,        // finished
    EXIT_REPLAY_WARN = 1,      // finished, but the playback reported a desync
    EXIT_REPLAY_BADARGS = 2,   // bad switch or file
    EXIT_REPLAY_GUI = 3        // stopped: the game opened a menu or dialog
};

const int FB_W = 256;
const int FB_H = 224;

// How long the game may poll the mouse without finishing a frame
// before the replay decides a menu or dialog is open.
const int GUI_POLLS = 200;
const int GUI_MS_RECORD = 300;
const int GUI_MS_PLAYBACK = 1000;

struct JoyState
{
    int num;                                              // num_joysticks
    unsigned int buttons;                                 // bit i: button[i].b
    int axis[MAX_JOYSTICK_STICKS][MAX_JOYSTICK_AXIS][3];  // pos, d1, d2
};

struct InputEvent
{
    int frame;
    char type;              // 'k' key, 'c' buffer entry, 'm' mouse
    int a, b, c, d;
};

struct JoyEvent
{
    int frame;
    int poll;               // -1: before the first frame
    JoyState st;
};

struct SeedEvent
{
    int frame;
    unsigned int value;
};

struct CfgVar
{
    const char *name;
    char kind;              // 'i' int, 'b' bool, 'y' byte
    void *p;
};

// The zc.cfg settings that change what a recording does. Recorded in the
// header; playback warns when they differ.
CfgVar cfg_vars[] =
{
    { "key_up",    'i', &DUkey },  { "key_down",  'i', &DDkey },
    { "key_left",  'i', &DLkey },  { "key_right", 'i', &DRkey },
    { "key_a",     'i', &Akey },   { "key_b",     'i', &Bkey },
    { "key_s",     'i', &Skey },   { "key_l",     'i', &Lkey },
    { "key_r",     'i', &Rkey },   { "key_p",     'i', &Pkey },
    { "key_ex1",   'i', &Exkey1 }, { "key_ex2",   'i', &Exkey2 },
    { "key_ex3",   'i', &Exkey3 }, { "key_ex4",   'i', &Exkey4 },
    { "key_cheatmod_a1", 'i', &cheat_modifier_keys[0] },
    { "key_cheatmod_a2", 'i', &cheat_modifier_keys[1] },
    { "key_cheatmod_b1", 'i', &cheat_modifier_keys[2] },
    { "key_cheatmod_b2", 'i', &cheat_modifier_keys[3] },
    { "btn_up",    'i', &DUbtn },  { "btn_down",  'i', &DDbtn },
    { "btn_left",  'i', &DLbtn },  { "btn_right", 'i', &DRbtn },
    { "btn_a",     'i', &Abtn },   { "btn_b",     'i', &Bbtn },
    { "btn_s",     'i', &Sbtn },   { "btn_m",     'i', &Mbtn },
    { "btn_l",     'i', &Lbtn },   { "btn_r",     'i', &Rbtn },
    { "btn_p",     'i', &Pbtn },
    { "btn_ex1",   'i', &Exbtn1 }, { "btn_ex2",   'i', &Exbtn2 },
    { "btn_ex3",   'i', &Exbtn3 }, { "btn_ex4",   'i', &Exbtn4 },
    { "joystick_index",  'i', &joystick_index },
    { "analog_movement", 'b', &analog_movement },
    { "name_entry_mode", 'i', &NameEntryMode },
    { "title",           'i', &title_version },
    { "translayers",     'b', &TransLayers },
    { "fastquit",        'b', &NESquit },
    { "clicktofreeze",   'b', &ClickToFreeze },
    { "epilepsy_flash_reduction", 'y', &epilepsyFlashReduction }
};
const int CFG_VAR_COUNT = int(sizeof(cfg_vars) / sizeof(cfg_vars[0]));

int cfg_get(const CfgVar &v)
{
    switch(v.kind)
    {
    case 'b': return *(bool *)v.p ? 1 : 0;
    case 'y': return *(byte *)v.p;
    default:  return *(int *)v.p;
    }
}

// ---- state -------------------------------------------------------------

Mode g_mode = MODE_OFF;
bool g_enabled = false;         // any replay switch given
bool g_unthrottled = false;
int  g_stop_after = 0;          // -frames N (0 = no limit)
bool g_seed_given = false;
unsigned int g_seed_value = 0;

std::string g_rec_path, g_play_path, g_hash_path, g_smoke_path;
FILE *g_rec = NULL;             // recording being written
FILE *g_hash = NULL;            // hash log being written

bool g_dump = false;            // -framedump
std::string g_dump_dir;
int g_dump_first = 0, g_dump_last = -1;

int  g_frame = 0;               // replay frame being computed
int  g_end = -1;                // playback: number of frames recorded
bool g_finished = false;
int  g_warnings = 0;

// What the game reads during g_frame.
char g_keys[KEY_MAX];
int  g_mouse[4];                // x, y, z, b
std::vector<int> g_chars;       // keyboard buffer entries (readkey() format)
size_t g_char_pos = 0;          // playback: next entry keypressed()/readkey() return
JoyState g_joy;
int  g_poll = 0;                // poll_joystick() calls in this frame

// Playback data.
std::vector<InputEvent> g_events;
size_t g_ev = 0;
std::vector<JoyEvent> g_joyev;
size_t g_jev = 0;
std::vector<SeedEvent> g_seeds;
size_t g_sev = 0;
struct CfgEntry { std::string name; int value; };
std::vector<CfgEntry> g_cfg;

// Frame hash of the frame shown in g_frame.
bool g_have_capture = false;
uint64_t g_fb_hash = 0, g_pal_hash = 0;

// -quest-smoke FILE: a progress log for the unattended run of a whole quest
// collection, so a run that "finished" can be told apart from one that never
// got into the game. Written every SMOKE_EVERY frames and when the player exits.
const int SMOKE_EVERY = 120;
FILE *g_smoke = NULL;
int g_smoke_playing = 0;        // frames with a game in progress
int g_smoke_first = -1;         // the first such frame
std::set<int> g_smoke_screens;  // (map << 8) | screen, every screen seen in play
std::set<uint64_t> g_smoke_hashes;  // distinct pictures shown

// Live mouse (the GUI defaults the game normally uses).
int (*g_orig_mouse_x)(void) = NULL;
int (*g_orig_mouse_y)(void) = NULL;
int (*g_orig_mouse_z)(void) = NULL;
int (*g_orig_mouse_b)(void) = NULL;

// Menu/dialog detection.
int  g_mouse_polls = 0;
bool g_gui = false;
std::chrono::steady_clock::time_point g_boundary_time;

// Keyboard callbacks (may run on Allegro's input thread).
const unsigned int RING_SIZE = 256;
int g_ring[RING_SIZE];
std::atomic<unsigned int> g_ring_head(0);   // written by the callback
std::atomic<unsigned int> g_ring_tail(0);   // written by the game thread
std::atomic<int> g_ring_lost(0);
std::atomic<bool> g_injecting(false);
std::atomic<int> g_inject_value(0);
std::atomic<bool> g_gui_atomic(false);
std::atomic<bool> g_in_logic(false);
std::atomic<int> g_logic_key_events(0);     // record: key events while a frame was computed
std::atomic<int> g_live_key_events(0);      // playback: keys pressed on the playback window
int g_logic_key_total = 0;
int g_live_key_total = 0;
bool g_in_atexit = false;   // Allegro may already be shut down

// ---- helpers -----------------------------------------------------------

void say(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf)-1] = 0;
    fprintf(stderr, "replay: %s\n", buf);
    fflush(stderr);
    al_trace("replay: %s\n", buf);
}

// A warning that also goes into the hash log, so it shows next to the
// frame it happened at.
void warn(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf)-1] = 0;
    ++g_warnings;
    say("warning at frame %d: %s", g_frame, buf);
    if(g_hash)
        fprintf(g_hash, "# warning at frame %d: %s\n", g_frame, buf);
}

void fail(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf)-1] = 0;
    say("error: %s", buf);
    exit(EXIT_REPLAY_BADARGS);
}

const char *g_key_names[KEY_MAX];

void init_key_names()
{
    for(int i = 0; i < KEY_MAX; i++)
        g_key_names[i] = NULL;
#define RK(k) g_key_names[KEY_##k] = #k
    RK(A); RK(B); RK(C); RK(D); RK(E); RK(F); RK(G); RK(H); RK(I); RK(J);
    RK(K); RK(L); RK(M); RK(N); RK(O); RK(P); RK(Q); RK(R); RK(S); RK(T);
    RK(U); RK(V); RK(W); RK(X); RK(Y); RK(Z);
    RK(0); RK(1); RK(2); RK(3); RK(4); RK(5); RK(6); RK(7); RK(8); RK(9);
    RK(0_PAD); RK(1_PAD); RK(2_PAD); RK(3_PAD); RK(4_PAD);
    RK(5_PAD); RK(6_PAD); RK(7_PAD); RK(8_PAD); RK(9_PAD);
    RK(F1); RK(F2); RK(F3); RK(F4); RK(F5); RK(F6);
    RK(F7); RK(F8); RK(F9); RK(F10); RK(F11); RK(F12);
    RK(ESC); RK(TILDE); RK(MINUS); RK(EQUALS); RK(BACKSPACE); RK(TAB);
    RK(OPENBRACE); RK(CLOSEBRACE); RK(ENTER); RK(COLON); RK(QUOTE);
    RK(BACKSLASH); RK(BACKSLASH2); RK(COMMA); RK(STOP); RK(SLASH); RK(SPACE);
    RK(INSERT); RK(DEL); RK(HOME); RK(END); RK(PGUP); RK(PGDN);
    RK(LEFT); RK(RIGHT); RK(UP); RK(DOWN);
    RK(SLASH_PAD); RK(ASTERISK); RK(MINUS_PAD); RK(PLUS_PAD); RK(DEL_PAD);
    RK(ENTER_PAD); RK(PRTSCR); RK(PAUSE); RK(ABNT_C1); RK(YEN); RK(KANA);
    RK(CONVERT); RK(NOCONVERT); RK(AT); RK(CIRCUMFLEX); RK(COLON2); RK(KANJI);
    RK(EQUALS_PAD); RK(BACKQUOTE); RK(SEMICOLON); RK(COMMAND);
    RK(LSHIFT); RK(RSHIFT); RK(LCONTROL); RK(RCONTROL); RK(ALT); RK(ALTGR);
    RK(LWIN); RK(RWIN); RK(MENU); RK(SCRLOCK); RK(NUMLOCK); RK(CAPSLOCK);
#undef RK
}

bool same_text(const char *a, const char *b)   // case-insensitive
{
    for(; *a && *b; ++a, ++b)
        if(tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    return *a == *b;
}

bool parse_int(const char *s, long *out)
{
    if(!s || !*s)
        return false;
    char *end = NULL;
    long v = strtol(s, &end, 0);
    if(!end || *end)
        return false;
    *out = v;
    return true;
}

int parse_key(const char *s)
{
    long v;
    if(parse_int(s, &v))
        return (v > 0 && v < KEY_MAX) ? int(v) : -1;
    for(int i = 1; i < KEY_MAX; i++)
        if(g_key_names[i] && same_text(g_key_names[i], s))
            return i;
    return -1;
}

void write_key_name(FILE *f, int k)
{
    if(g_key_names[k])
        fputs(g_key_names[k], f);
    else
        fprintf(f, "%d", k);
}

uint64_t fnv1a(uint64_t h, const unsigned char *p, int n)
{
    for(int i = 0; i < n; i++)
    {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

const uint64_t FNV_BASIS = 0xcbf29ce484222325ULL;

// ---- joystick ------------------------------------------------------------

int joy_index()
{
    return (joystick_index >= 0 && joystick_index < MAX_JOYSTICKS) ? joystick_index : 0;
}

void joy_snapshot(JoyState &s)
{
    memset(&s, 0, sizeof(s));
    s.num = num_joysticks;
    JOYSTICK_INFO &j = joy[joy_index()];
    for(int i = 0; i < MAX_JOYSTICK_BUTTONS && i < 32; i++)
        if(j.button[i].b)
            s.buttons |= 1u << i;
    for(int st = 0; st < MAX_JOYSTICK_STICKS; st++)
        for(int ax = 0; ax < MAX_JOYSTICK_AXIS; ax++)
        {
            s.axis[st][ax][0] = j.stick[st].axis[ax].pos;
            s.axis[st][ax][1] = j.stick[st].axis[ax].d1;
            s.axis[st][ax][2] = j.stick[st].axis[ax].d2;
        }
}

void joy_apply(const JoyState &s)
{
    num_joysticks = s.num;
    JOYSTICK_INFO &j = joy[joy_index()];
    for(int i = 0; i < MAX_JOYSTICK_BUTTONS && i < 32; i++)
        j.button[i].b = (s.buttons >> i) & 1;
    for(int st = 0; st < MAX_JOYSTICK_STICKS; st++)
        for(int ax = 0; ax < MAX_JOYSTICK_AXIS; ax++)
        {
            j.stick[st].axis[ax].pos = s.axis[st][ax][0];
            j.stick[st].axis[ax].d1 = s.axis[st][ax][1];
            j.stick[st].axis[ax].d2 = s.axis[st][ax][2];
        }
}

void joy_write(int frame, int poll, const JoyState &s)
{
    fprintf(g_rec, "%d joy %d %d 0x%x", frame, poll, s.num, s.buttons);
    for(int st = 0; st < MAX_JOYSTICK_STICKS; st++)
        for(int ax = 0; ax < MAX_JOYSTICK_AXIS; ax++)
            fprintf(g_rec, " %d %d %d", s.axis[st][ax][0], s.axis[st][ax][1], s.axis[st][ax][2]);
    fputc('\n', g_rec);
}

// ---- keyboard callbacks ----------------------------------------------------

int replay_keyboard_callback(int k)
{
    // Our own injection at the frame boundary.
    if(g_injecting.load() && k == g_inject_value.load())
        return k;

    // A menu or dialog is open while recording: let it have the key.
    if(g_gui_atomic.load())
        return k;

    if(g_mode == MODE_RECORD)
    {
        // Deferred to the next frame boundary.
        unsigned int head = g_ring_head.load();
        if(head - g_ring_tail.load() < RING_SIZE)
        {
            g_ring[head % RING_SIZE] = k;
            g_ring_head.store(head + 1);
        }
        else
            ++g_ring_lost;
    }

    // Playback: live keys never reach the buffer (the low-level callback
    // counts them).
    return 0;
}

void replay_keyboard_lowlevel_callback(int scancode)
{
    int k = scancode & 0x7F;
    bool down = !(scancode & 0x80);
    if(k <= 0 || k >= KEY_MAX)
        return;

    if(g_mode == MODE_RECORD)
    {
        // Only changes matter: the driver also "releases" every key when
        // the window loses the focus or the video mode changes.
        if(g_in_logic.load() && down != (g_keys[k] != 0))
            ++g_logic_key_events;
    }
    else if(down)
        ++g_live_key_events;
}

// Playback has no keyboard driver (nothing from the OS reaches key[]);
// keypressed()/readkey() read this frame's recorded buffer entries. Allegro's
// clear_keybuf() empties it through these hooks.
int replay_keypressed_hook(void)
{
    return g_char_pos < g_chars.size() ? TRUE : FALSE;
}

int replay_readkey_hook(void)
{
    // The real readkey() would wait for a key; a playback cannot.
    return g_char_pos < g_chars.size() ? g_chars[g_char_pos++] : 0;
}

// ---- smoke progress log ----------------------------------------------------

void smoke_write(const char *kind, const char *why)
{
    if(!g_smoke)
        return;
    bool in_game = Playing && !Quit && game != NULL;
    fprintf(g_smoke, "%s frame=%d end=%d playing=%d first=%d screens=%u hashes=%u warnings=%d in_game=%d quit=%d map=%d scr=%d dmap=%d life=%d",
            kind, g_frame, g_end, g_smoke_playing, g_smoke_first, (unsigned)g_smoke_screens.size(),
            (unsigned)g_smoke_hashes.size(), g_warnings, in_game ? 1 : 0, Quit,
            in_game ? currmap : -1, in_game ? currscr : -1, in_game ? currdmap : -1, in_game ? int(game->get_life()) : -1);
    if(why)
        fprintf(g_smoke, " why=%s", why);
    fputc('\n', g_smoke);
    fflush(g_smoke);
}

void smoke_frame()
{
    if(Playing && !Quit && game != NULL)
    {
        if(g_smoke_first < 0)
            g_smoke_first = g_frame;
        ++g_smoke_playing;
        g_smoke_screens.insert((currmap << 8) | currscr);
    }
    if(g_frame % SMOKE_EVERY == 0)
        smoke_write("frame", NULL);
}

// ---- mouse -------------------------------------------------------------

void finish(int code, const char *why);

void note_mouse_poll()
{
    if(g_gui || g_mode == MODE_OFF)
        return;
    if(++g_mouse_polls < GUI_POLLS)
        return;
    int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - g_boundary_time).count();
    if(ms < (g_mode == MODE_PLAYBACK ? GUI_MS_PLAYBACK : GUI_MS_RECORD))
        return;

    g_gui = true;
    g_gui_atomic.store(true);

    if(g_mode == MODE_PLAYBACK)
    {
        finish(EXIT_REPLAY_GUI, "the game is waiting in a menu or dialog; recordings do not contain menu input, playback stops here");
        exit(EXIT_REPLAY_GUI);
    }

    fprintf(g_rec, "# %d a menu or dialog opened: its input is not recorded, playback will stop here\n", g_frame);
    say("frame %d: a menu or dialog opened; the recording cannot be played back past this point", g_frame);
}

int replay_mouse_x(void)
{
    note_mouse_poll();
    return (g_gui && g_mode == MODE_RECORD) ? g_orig_mouse_x() : g_mouse[0];
}

int replay_mouse_y(void)
{
    note_mouse_poll();
    return (g_gui && g_mode == MODE_RECORD) ? g_orig_mouse_y() : g_mouse[1];
}

int replay_mouse_z(void)
{
    note_mouse_poll();
    return (g_gui && g_mode == MODE_RECORD) ? g_orig_mouse_z() : g_mouse[2];
}

int replay_mouse_b(void)
{
    note_mouse_poll();
    return (g_gui && g_mode == MODE_RECORD) ? g_orig_mouse_b() : g_mouse[3];
}

// ---- finishing -------------------------------------------------------------

void finish(int code, const char *why)
{
    if(g_finished)
        return;
    g_finished = true;

    if(why)
    {
        say("frame %d: %s", g_frame, why);
        if(g_hash)
            fprintf(g_hash, "# stopped at frame %d: %s\n", g_frame, why);
    }

    if(g_mode == MODE_RECORD)
    {
        int lost = g_ring_lost.load();
        if(lost)
            say("%d keyboard buffer entries were lost (more than %u in one frame)", lost, RING_SIZE);
        if(g_logic_key_total)
            say("%d key events arrived while a frame was being computed (marked in the recording); the playback may differ from this session at those frames", g_logic_key_total);
    }

    if(g_mode == MODE_PLAYBACK && g_live_key_total + g_live_key_events.load())
        say("%d key(s) were pressed on the playback window", g_live_key_total + g_live_key_events.load());

    if(g_smoke)
    {
        smoke_write("end", why);
        fclose(g_smoke);
        g_smoke = NULL;
    }

    if(g_rec)
    {
        fprintf(g_rec, "end %d\n", g_frame);
        fclose(g_rec);
        g_rec = NULL;
        say("recorded %d frames to %s", g_frame, g_rec_path.c_str());
    }

    if(g_hash)
    {
        fprintf(g_hash, "# end %d frames\n", g_frame);
        fclose(g_hash);
        g_hash = NULL;
    }

    if(g_mode == MODE_PLAYBACK)
        say("played %d of %d frames from %s, %d warning(s)", g_frame, g_end, g_play_path.c_str(), g_warnings);

    (void)code;
}

void replay_atexit()
{
    g_in_atexit = true;
    finish(EXIT_REPLAY_OK, NULL);
}

void stop_now(int code, const char *why)
{
    finish(code, why);
    if(code == EXIT_REPLAY_OK && g_warnings && g_mode == MODE_PLAYBACK)
        code = EXIT_REPLAY_WARN;
    exit(code);
}

// ---- the recording file ------------------------------------------------------

void load_recording(const char *path)
{
    FILE *f = fopen(path, "rt");
    if(!f)
        fail("cannot open recording %s", path);

    char line[4096];
    int lineno = 0;
    bool header = false;
    int last_frame = 0;
    int max_frame = -1;

    while(fgets(line, sizeof(line), f))
    {
        ++lineno;
        char *tok[64];
        int n = 0;
        for(char *t = strtok(line, " \t\r\n"); t && n < 64; t = strtok(NULL, " \t\r\n"))
            tok[n++] = t;
        if(n == 0 || tok[0][0] == '#')
            continue;

        if(!header)
        {
            long ver;
            if(n < 2 || strcmp(tok[0], "zcreplay") || !parse_int(tok[1], &ver))
                fail("%s:%d: not a recording (expected \"zcreplay 1\")", path, lineno);
            if(ver != 1)
                fail("%s:%d: unsupported recording version %ld", path, lineno, ver);
            header = true;
            continue;
        }

        if(!strcmp(tok[0], "cfg"))
        {
            long v;
            if(n != 3 || !parse_int(tok[2], &v))
                fail("%s:%d: bad cfg line", path, lineno);
            CfgEntry e;
            e.name = tok[1];
            e.value = int(v);
            g_cfg.push_back(e);
            continue;
        }

        if(!strcmp(tok[0], "end"))
        {
            long v;
            if(n != 2 || !parse_int(tok[1], &v) || v < 0)
                fail("%s:%d: bad end line", path, lineno);
            g_end = int(v);
            break;
        }

        long frame;
        if(n < 2 || !parse_int(tok[0], &frame) || frame < 0)
            fail("%s:%d: expected <frame> <event> ...", path, lineno);
        if(frame < last_frame)
            fail("%s:%d: frame %ld is before frame %d", path, lineno, frame, last_frame);
        last_frame = int(frame);
        if(int(frame) > max_frame)
            max_frame = int(frame);

        const char *type = tok[1];
        long v[64];
        for(int i = 2; i < n; i++)
        {
            if(!strcmp(type, "key") && i == 2)
            {
                v[i] = parse_key(tok[i]);
                if(v[i] < 0)
                    fail("%s:%d: unknown key %s", path, lineno, tok[i]);
            }
            else if(!parse_int(tok[i], &v[i]))
                fail("%s:%d: bad number %s", path, lineno, tok[i]);
        }

        InputEvent e;
        memset(&e, 0, sizeof(e));
        e.frame = int(frame);

        if(!strcmp(type, "key"))
        {
            if(n != 4)
                fail("%s:%d: expected <frame> key <key> <value>", path, lineno);
            e.type = 'k';
            e.a = int(v[2]);
            e.b = int(v[3]);
            g_events.push_back(e);
        }
        else if(!strcmp(type, "char"))
        {
            if(n != 3)
                fail("%s:%d: expected <frame> char <value>", path, lineno);
            e.type = 'c';
            e.a = int(v[2]);
            g_events.push_back(e);
        }
        else if(!strcmp(type, "mouse"))
        {
            if(n != 6)
                fail("%s:%d: expected <frame> mouse <x> <y> <z> <b>", path, lineno);
            e.type = 'm';
            e.a = int(v[2]);
            e.b = int(v[3]);
            e.c = int(v[4]);
            e.d = int(v[5]);
            g_events.push_back(e);
        }
        else if(!strcmp(type, "seed"))
        {
            if(n != 3)
                fail("%s:%d: expected <frame> seed <value>", path, lineno);
            SeedEvent s;
            s.frame = int(frame);
            s.value = (unsigned int)v[2];
            g_seeds.push_back(s);
        }
        else if(!strcmp(type, "joy"))
        {
            const int axes = MAX_JOYSTICK_STICKS * MAX_JOYSTICK_AXIS * 3;
            if(n != 5 + axes)
                fail("%s:%d: expected <frame> joy <poll> <num> <buttons> and %d axis values", path, lineno, axes);
            JoyEvent j;
            memset(&j, 0, sizeof(j));
            j.frame = int(frame);
            j.poll = int(v[2]);
            j.st.num = int(v[3]);
            j.st.buttons = (unsigned int)v[4];
            int *dst = &j.st.axis[0][0][0];
            for(int i = 0; i < axes; i++)
                dst[i] = int(v[5 + i]);
            g_joyev.push_back(j);
        }
        else
            fail("%s:%d: unknown event %s", path, lineno, type);
    }

    fclose(f);

    if(!header)
        fail("%s: empty file, not a recording", path);

    if(g_end < 0)
    {
        g_end = max_frame + 1;
        say("%s has no \"end\" line (the recording was cut short?); playing %d frames", path, g_end);
    }
}

void check_cfg()
{
    for(size_t i = 0; i < g_cfg.size(); i++)
    {
        for(int j = 0; j < CFG_VAR_COUNT; j++)
        {
            if(g_cfg[i].name != cfg_vars[j].name)
                continue;
            int now = cfg_get(cfg_vars[j]);
            if(now != g_cfg[i].value)
                warn("zc.cfg differs from the recording: %s is %d here, %d in the recording",
                     cfg_vars[j].name, now, g_cfg[i].value);
        }
    }
}

void write_header(int argc, char *argv[])
{
    fprintf(g_rec, "zcreplay 1\n");
    fprintf(g_rec, "# Zelda Classic input recording, see docs/zplayer-replay.md\n");
#ifdef ZC_PORT
    fprintf(g_rec, "# recorded with zplayer\n");
#else
    fprintf(g_rec, "# recorded with the Allegro build\n");
#endif
    fprintf(g_rec, "# args:");
    for(int i = 1; i < argc; i++)
        fprintf(g_rec, " %s", argv[i]);
    fprintf(g_rec, "\n");
    for(int j = 0; j < CFG_VAR_COUNT; j++)
        fprintf(g_rec, "cfg %s %d\n", cfg_vars[j].name, cfg_get(cfg_vars[j]));
}

// ---- input at the frame boundary -------------------------------------------

void inject_buffer()
{
    clear_keybuf();
    for(size_t i = 0; i < g_chars.size(); i++)
    {
        g_inject_value.store(g_chars[i]);
        g_injecting.store(true);
        simulate_keypress(g_chars[i]);
        g_injecting.store(false);
    }
}

// Sets up what the game reads during g_frame.
void begin_frame_input()
{
    g_poll = 0;
    g_mouse_polls = 0;
    g_gui = false;
    g_gui_atomic.store(false);
    g_boundary_time = std::chrono::steady_clock::now();
    g_chars.clear();
    g_char_pos = 0;

    if(g_mode == MODE_RECORD)
    {
        int racy = g_logic_key_events.exchange(0);
        if(racy && g_frame > 0)
        {
            g_logic_key_total += racy;
            fprintf(g_rec, "# %d: %d key event(s) arrived while frame %d was computed\n", g_frame, racy, g_frame - 1);
        }

        for(int k = 1; k < KEY_MAX; k++)
        {
            char v = key[k];
            if(v != g_keys[k])
            {
                fprintf(g_rec, "%d key ", g_frame);
                write_key_name(g_rec, k);
                fprintf(g_rec, " %d\n", int(v));
                g_keys[k] = v;
            }
        }

        int m[4] = { g_orig_mouse_x(), g_orig_mouse_y(), g_orig_mouse_z(), g_orig_mouse_b() };
        if(memcmp(m, g_mouse, sizeof(m)))
        {
            fprintf(g_rec, "%d mouse %d %d %d %d\n", g_frame, m[0], m[1], m[2], m[3]);
            memcpy(g_mouse, m, sizeof(m));
        }

        unsigned int tail = g_ring_tail.load();
        unsigned int head = g_ring_head.load();
        while(tail != head)
        {
            int c = g_ring[tail % RING_SIZE];
            ++tail;
            g_chars.push_back(c);
            fprintf(g_rec, "%d char 0x%04x\n", g_frame, c);
        }
        g_ring_tail.store(tail);
    }
    else if(g_mode == MODE_PLAYBACK)
    {
        // Lets zplayer handle its window events here, before key[] is set
        // (the playback skips the other places that do it: the throttle,
        // the GUI mouse functions, keypressed()). Harmless in Allegro.
        poll_mouse();

        // Keys pressed on the playback window (only where the platform
        // still updates key[] without a keyboard driver).
        int live = g_live_key_events.exchange(0);
        if(live)
        {
            g_live_key_total += live;
            warn("%d key(s) pressed on the playback window during frame %d; the playback may not be exact from here", live, g_frame - 1);
        }

        while(g_ev < g_events.size() && g_events[g_ev].frame <= g_frame)
        {
            const InputEvent &e = g_events[g_ev++];
            switch(e.type)
            {
            case 'k':
                g_keys[e.a] = char(e.b);
                break;
            case 'c':
                g_chars.push_back(e.a);
                break;
            case 'm':
                g_mouse[0] = e.a;
                g_mouse[1] = e.b;
                g_mouse[2] = e.c;
                g_mouse[3] = e.d;
                break;
            }
        }
    }

    if(g_mode == MODE_RECORD)
        inject_buffer();
    else
    {
        for(int k = 1; k < KEY_MAX; k++)
            key[k] = g_keys[k];
    }

    g_in_logic.store(true);
}

void start(int argc, char *argv[])
{
    memset(g_keys, 0, sizeof(g_keys));
    memset(g_mouse, 0, sizeof(g_mouse));
    memset(&g_joy, 0, sizeof(g_joy));

    g_orig_mouse_x = gui_mouse_x;
    g_orig_mouse_y = gui_mouse_y;
    g_orig_mouse_z = gui_mouse_z;
    g_orig_mouse_b = gui_mouse_b;
    gui_mouse_x = replay_mouse_x;
    gui_mouse_y = replay_mouse_y;
    gui_mouse_z = replay_mouse_z;
    gui_mouse_b = replay_mouse_b;

    keyboard_callback = replay_keyboard_callback;
    keyboard_lowlevel_callback = replay_keyboard_lowlevel_callback;
    clear_keybuf();

    if(g_mode == MODE_PLAYBACK)
    {
        // No keyboard driver: the OS can no longer change key[] (a key
        // pressed on the window, or the "release all keys" when the window
        // loses the focus). The buffer comes from the recording.
        remove_keyboard();
        for(int k = 1; k < KEY_MAX; k++)
            key[k] = 0;
        install_keyboard_hooks(replay_keypressed_hook, replay_readkey_hook);
    }

    if(g_mode == MODE_RECORD)
    {
        write_header(argc, argv);
        joy_snapshot(g_joy);
        JoyState zero;
        memset(&zero, 0, sizeof(zero));
        if(memcmp(&zero, &g_joy, sizeof(zero)))
            joy_write(0, -1, g_joy);
    }
    else
    {
        // The joystick comes from the recording only.
        remove_joystick();
        for(int i = 0; i < MAX_JOYSTICKS; i++)
        {
            JOYSTICK_INFO &j = joy[i];
            for(int b = 0; b < MAX_JOYSTICK_BUTTONS; b++)
                j.button[b].b = 0;
            for(int st = 0; st < MAX_JOYSTICK_STICKS; st++)
                for(int ax = 0; ax < MAX_JOYSTICK_AXIS; ax++)
                {
                    j.stick[st].axis[ax].pos = 0;
                    j.stick[st].axis[ax].d1 = 0;
                    j.stick[st].axis[ax].d2 = 0;
                }
        }
        if(g_jev < g_joyev.size() && g_joyev[g_jev].frame == 0 && g_joyev[g_jev].poll < 0)
            g_joy = g_joyev[g_jev++].st;
        joy_apply(g_joy);
        check_cfg();
    }

    begin_frame_input();
}

} // namespace

// ---- public hooks ------------------------------------------------------------

void replay_init(int argc, char *argv[])
{
    init_key_names();

    for(int i = 1; i < argc; i++)
    {
        const char *a = argv[i];
        bool has1 = i + 1 < argc;
        long v;

        if(same_text(a, "-record") || same_text(a, "-replay") || same_text(a, "-framehash"))
        {
            if(!has1)
                fail("%s needs a file name", a);
            if(same_text(a, "-record"))
                g_rec_path = argv[++i];
            else if(same_text(a, "-replay"))
                g_play_path = argv[++i];
            else
                g_hash_path = argv[++i];
            g_enabled = true;
        }
        else if(same_text(a, "-quest-smoke"))
        {
            if(!has1)
                fail("%s needs a file name", a);
            g_smoke_path = argv[++i];
            g_enabled = true;
        }
        else if(same_text(a, "-frames") || same_text(a, "-replay-seed"))
        {
            if(!has1 || !parse_int(argv[i+1], &v) || v < 0)
                fail("%s needs a number", a);
            if(same_text(a, "-frames"))
                g_stop_after = int(v);
            else
            {
                g_seed_given = true;
                g_seed_value = (unsigned int)v;
            }
            ++i;
            g_enabled = true;
        }
        else if(same_text(a, "-unthrottled"))
        {
            g_unthrottled = true;
        }
        else if(same_text(a, "-framedump"))
        {
            long first, last;
            if(i + 3 >= argc || !parse_int(argv[i+2], &first) || !parse_int(argv[i+3], &last))
                fail("-framedump needs <directory> <first frame> <last frame>");
            g_dump = true;
            g_dump_dir = argv[i+1];
            g_dump_first = int(first);
            g_dump_last = int(last);
            i += 3;
            g_enabled = true;
        }
    }

    if(!g_rec_path.empty() && !g_play_path.empty())
        fail("-record and -replay cannot be used together");

    if(!g_enabled)
        return;

    if(!g_play_path.empty())
    {
        g_mode = MODE_PLAYBACK;
        load_recording(g_play_path.c_str());
        if(g_seed_given)
            say("-replay-seed is ignored during playback (the recording has its seeds)");
    }
    else if(!g_rec_path.empty())
    {
        g_mode = MODE_RECORD;
        g_rec = fopen(g_rec_path.c_str(), "wt");
        if(!g_rec)
            fail("cannot write recording %s", g_rec_path.c_str());
    }

    if(!g_hash_path.empty())
    {
        g_hash = fopen(g_hash_path.c_str(), "wt");
        if(!g_hash)
            fail("cannot write hash log %s", g_hash_path.c_str());
        fprintf(g_hash, "zcframehash 1\n");
        fprintf(g_hash, "# <frame> <hash of the 256x224 frame> <hash of the palette>, FNV-1a 64\n");
    }

    if(!g_smoke_path.empty())
    {
        g_smoke = fopen(g_smoke_path.c_str(), "wt");
        if(!g_smoke)
            fail("cannot write smoke log %s", g_smoke_path.c_str());
        smoke_write("start", NULL);
    }

    atexit(replay_atexit);

    if(g_mode != MODE_OFF)
        start(argc, argv);

    if(g_mode == MODE_PLAYBACK)
        say("playing %s (%d frames)%s", g_play_path.c_str(), g_end, g_unthrottled ? ", unthrottled" : "");
    else if(g_mode == MODE_RECORD)
        say("recording to %s", g_rec_path.c_str());
}

bool replay_active()
{
    return g_mode != MODE_OFF;
}

bool replay_unthrottled()
{
    return g_unthrottled;
}

unsigned int replay_seed(unsigned int live_seed)
{
    if(g_mode == MODE_RECORD)
    {
        unsigned int v = g_seed_given ? g_seed_value : live_seed;
        fprintf(g_rec, "%d seed %u\n", g_frame, v);
        return v;
    }

    if(g_mode == MODE_PLAYBACK)
    {
        if(g_sev < g_seeds.size())
        {
            const SeedEvent &s = g_seeds[g_sev++];
            if(s.frame != g_frame)
                warn("the game seeds its random numbers at frame %d, the recording did at frame %d (desync)", g_frame, s.frame);
            return s.value;
        }
        warn("the game seeds its random numbers, the recording has no seed left (desync)");
        return 0;
    }

    return live_seed;
}

void replay_capture_frame(BITMAP *frame)
{
    g_in_logic.store(false);

    if(!g_hash && !g_dump && !g_smoke)
        return;

    PALETTE pal;
    get_palette(pal);

    if(g_hash || g_smoke)
    {
        int w = zc_min(frame->w, FB_W);
        int h = zc_min(frame->h, FB_H);
        uint64_t fh = FNV_BASIS;
        for(int y = 0; y < h; y++)
            fh = fnv1a(fh, frame->line[y], w);
        uint64_t ph = FNV_BASIS;
        for(int i = 0; i < PAL_SIZE; i++)
        {
            unsigned char rgb[3] = { pal[i].r, pal[i].g, pal[i].b };
            ph = fnv1a(ph, rgb, 3);
        }
        g_fb_hash = fh;
        g_pal_hash = ph;
        g_have_capture = true;
    }

    if(g_dump && g_frame >= g_dump_first && g_frame <= g_dump_last)
    {
        char path[2048];
        snprintf(path, sizeof(path), "%s/frame%06d.bmp", g_dump_dir.c_str(), g_frame);
        path[sizeof(path)-1] = 0;
        if(save_bmp(path, frame, pal) != 0)
            say("cannot write %s", path);
    }
}

void replay_frame_boundary()
{
    if(!g_enabled || g_finished)
        return;

    g_in_logic.store(false);

    if(g_hash && g_have_capture)
        fprintf(g_hash, "%d %016" PRIx64 " %016" PRIx64 "\n", g_frame, g_fb_hash, g_pal_hash);
    if(g_smoke && g_have_capture)
        g_smoke_hashes.insert(g_fb_hash ^ g_pal_hash);
    g_have_capture = false;

    ++g_frame;

    if(g_smoke)
        smoke_frame();

    if((g_frame & 63) == 0)
    {
        if(g_hash) fflush(g_hash);
        if(g_rec) fflush(g_rec);
    }

    if(g_stop_after > 0 && g_frame >= g_stop_after)
        stop_now(EXIT_REPLAY_OK, NULL);

    if(g_mode == MODE_PLAYBACK && g_frame >= g_end)
        stop_now(EXIT_REPLAY_OK, NULL);

    if(g_mode != MODE_OFF)
        begin_frame_input();
}

void replay_joystick_polled()
{
    if(g_mode == MODE_OFF)
        return;

    int p = g_poll++;

    if(g_mode == MODE_RECORD)
    {
        JoyState s;
        joy_snapshot(s);
        if(memcmp(&s, &g_joy, sizeof(s)))
        {
            joy_write(g_frame, p, s);
            g_joy = s;
        }
        return;
    }

    while(g_jev < g_joyev.size() &&
          (g_joyev[g_jev].frame < g_frame || (g_joyev[g_jev].frame == g_frame && g_joyev[g_jev].poll < p)))
    {
        warn("joystick state recorded for frame %d poll %d was not used (desync)", g_joyev[g_jev].frame, g_joyev[g_jev].poll);
        g_joy = g_joyev[g_jev++].st;
    }

    if(g_jev < g_joyev.size() && g_joyev[g_jev].frame == g_frame && g_joyev[g_jev].poll == p)
        g_joy = g_joyev[g_jev++].st;

    joy_apply(g_joy);
}

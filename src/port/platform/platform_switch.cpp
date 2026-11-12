// zcport Nintendo Switch platform (homebrew, libnx): fills the zcport/platform.h
// hook table with libnx and OpenGL ES (no SDL, no Vulkan). The structure
// follows platform_sdl.cpp:
//
//   - the platform thread owns EGL and Dear ImGui, reads the pad and the applet
//     messages, and presents at the display's rate (gles_present_switch.cpp).
//     The game never waits for the display;
//   - the game thread converts the screen to RGBA through _current_palette at
//     most once per refresh, from pump_events, and hands it over;
//   - if no frame came for 100 ms the platform thread copies the screen itself,
//     so a long load still shows what the game drew;
//   - input: the pad is joystick 0 for the game (see hook_joy_poll for the
//     button order) and a gamepad for ImGui. While a UI session is open the
//     game sees no new presses, as on the other platforms;
//   - sound: an audout thread pulling 48 kHz stereo blocks from the zcport
//     mixer. The audio_lock hooks stay NULL: zcport then excludes the mix
//     callback with its own mutex;
//   - exit: HOME's close (an applet exit request) reaches the game through its
//     close button callback, and the game exits and saves as usual.
//
// Test aid: when "padscript.txt" exists in the game folder the pad is read from
// it instead, one "<buttons|-> <frames>" step per line (buttons joined by '+';
// the word EXIT stands for HOME's close). An emulator cannot be sent input
// without taking the desktop's focus.
//
// Written from scratch for zcport (no Allegro code).

#if defined(__SWITCH__)

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <malloc.h>

#include <allegro.h>
#include <allegro/internal/aintern.h>
#include "zcport/core.h"
#include "zcport/display.h"
#include "zcport/input.h"
#include "zcport/midi_backend.h"
#include "zcport/platform.h"
#include "zcport/platform_sdl.h"
#include "zcport/ui.h"
#include "ui_platform.h"
#include "gles_present_switch.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <switch.h>

namespace zcport_sdl
{
   // midi_tsf.cpp: TinySoundFont on "soundfont.sf2" in the working directory.
   const ZCPORT_MIDI_BACKEND *midi_backend();
}

// src/zelda.cpp: writes the saved games and the settings.
extern "C" void zplayer_switch_final_save(void);

namespace
{

const int kFbWidth = 1280;
const int kFbHeight = 720;
const int kRefreshHz = 60;
const uint64_t kFramePeriodUs = 1000000u / kRefreshHz;
const uint64_t kExitGraceUs = 10000000;   // libnx allows 15 s once exit is requested

const char *const kExecutableName = "sdmc:/switch/zplayer/zplayer.nro";

uint64_t now_us()
{
   return armTicksToNs(armGetSystemTick()) / 1000;
}

// ---------------------------------------------------------------------------
// pixel conversion (BITMAP -> bytes R, G, B, A)
// ---------------------------------------------------------------------------

// 6-bit palette component -> 8 bits, as the other platforms show it.
inline uint32_t expand6(int v)
{
   v &= 63;
   return (uint32_t)((v << 2) | (v >> 4));
}

inline uint32_t expand5(int v)
{
   v &= 31;
   return (uint32_t)((v << 3) | (v >> 2));
}

inline uint32_t make_rgba(uint32_t r, uint32_t g, uint32_t b)
{
   return 0xFF000000u | (b << 16) | (g << 8) | r;
}

void build_lut(const RGB *pal, uint32_t lut[256])
{
   for (int i = 0; i < 256; i++)
      lut[i] = make_rgba(expand6(pal[i].r), expand6(pal[i].g), expand6(pal[i].b));
}

inline uint32_t pixel_to_rgba(int depth, const unsigned char *row, int x)
{
   switch (depth) {
      case 15: {
         unsigned c = ((const uint16_t *)row)[x];
         return make_rgba(expand5(c >> 10), expand5(c >> 5), expand5(c));
      }
      case 16: {
         unsigned c = ((const uint16_t *)row)[x];
         unsigned g = (c >> 5) & 63;
         return make_rgba(expand5(c >> 11), (g << 2) | (g >> 4), expand5(c));
      }
      case 24: {
         const unsigned char *p = row + x * 3;
         return make_rgba(p[2], p[1], p[0]);
      }
      case 32: {
         uint32_t c = ((const uint32_t *)row)[x];
         return make_rgba((c >> 16) & 255, (c >> 8) & 255, c & 255);
      }
   }
   return 0xFF000000u;
}

void convert_bitmap(BITMAP *bmp, const uint32_t *lut, uint32_t *dst)
{
   const int w = bmp->w, h = bmp->h;
   const int depth = bitmap_color_depth(bmp);
   for (int y = 0; y < h; y++) {
      const unsigned char *row = bmp->line[y];
      uint32_t *out = dst + (size_t)y * w;
      if (depth == 8) {
         for (int x = 0; x < w; x++)
            out[x] = lut[row[x]];
      }
      else {
         for (int x = 0; x < w; x++)
            out[x] = pixel_to_rgba(depth, row, x);
      }
   }
}

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

struct Frame
{
   std::vector<uint32_t> px;
   int w, h;
   Frame() : w(0), h(0) {}
};

struct State
{
   // the platform thread
   Thread thread;
   bool running;
   std::atomic<bool> quit;
   std::mutex init_mutex;
   std::condition_variable init_cv;
   bool init_done, init_ok;
   std::string init_error;

   // frames
   std::mutex frame_mutex;       // pending, display
   Frame pending;
   bool pending_new;
   BITMAP *display;              // what the game shows (for self-snapshots)
   Frame work;                   // game thread scratch
   Frame shown;                  // platform thread scratch
   std::atomic<uint64_t> last_handover_us;
   std::atomic<unsigned> handovers;

   // mode
   std::atomic<int> disp_w, disp_h;
   std::atomic<bool> mode_active, windowed;

   // input the game sees
   std::mutex input_mutex;
   u64 buttons;
   HidAnalogStickState sticks[2];

   // exit
   std::atomic<bool> exit_requested;
   std::atomic<uint64_t> exit_request_us;

   State()
      : running(false), quit(false), init_done(false), init_ok(false), pending_new(false), display(NULL),
        last_handover_us(0), handovers(0), disp_w(0), disp_h(0), mode_active(false), windowed(false),
        buttons(0), exit_requested(false), exit_request_us(0)
   {
      memset(&thread, 0, sizeof(thread));
      memset(sticks, 0, sizeof(sticks));
   }
};

State g;

// ---------------------------------------------------------------------------
// zcport/display.h: the one screen. Only the picture settings mean anything.
// ---------------------------------------------------------------------------

std::mutex g_display_mutex;
zcdisplay::Settings g_settings;
bool g_settings_ready = false;
zcdisplay::LoadSettingsFn g_settings_loader = NULL;

zcdisplay::Settings display_defaults()
{
   zcdisplay::Settings s;
   s.mode = zcdisplay::MODE_BORDERLESS;
   s.fullscreen_mode = zcdisplay::MODE_BORDERLESS;
   s.window_w = s.window_h = 0;
   s.exclusive_w = s.exclusive_h = 0;
   s.exclusive_hz = 0;
   s.display = 1;
   s.scaling = zcdisplay::SCALING_FIT;
   s.vsync = true;
   s.show_fps = false;
   s.ui_scale = 1.0f;
   s.touch_mode = zcdisplay::TOUCH_NEVER;
   s.touch_size = 1.0f;
   s.touch_opacity = 0.6f;
   s.renderer = zcdisplay::RENDERER_AUTO;   // one renderer there (OpenGL ES)
   return s;
}

zcdisplay::Settings display_get()
{
   std::lock_guard<std::mutex> guard(g_display_mutex);
   if (!g_settings_ready) {
      g_settings = display_defaults();
      g_settings_ready = true;
   }
   return g_settings;
}

void display_set(const zcdisplay::Settings &s)
{
   {
      std::lock_guard<std::mutex> guard(g_display_mutex);
      g_settings = s;
      g_settings.vsync = true;      // the display always paces the platform thread
      g_settings_ready = true;
   }
   zcui::set_user_scale(s.ui_scale);
}

// Game thread, at the first graphics mode: the saved settings.
void display_before_open()
{
   static bool loaded = false;
   if (loaded)
      return;
   loaded = true;
   zcdisplay::Settings s = display_defaults();
   if (g_settings_loader)
      g_settings_loader(&s);
   display_set(s);
}

void picture_rect(int w, int h, int scaling, int *x, int *y, int *dw, int *dh)
{
   if (w <= 0 || h <= 0) {
      *x = *y = *dw = *dh = 0;
      return;
   }
   float s = std::min((float)kFbWidth / w, (float)kFbHeight / h);
   if (scaling == zcdisplay::SCALING_STRETCH) {
      *x = *y = 0;
      *dw = kFbWidth;
      *dh = kFbHeight;
      return;
   }
   if (scaling == zcdisplay::SCALING_INTEGER && s >= 1.0f)
      s = floorf(s);
   *dw = (int)(w * s);
   *dh = (int)(h * s);
   *x = (kFbWidth - *dw) / 2;
   *y = (kFbHeight - *dh) / 2;
}

// ---------------------------------------------------------------------------
// platform thread: the pad script (test aid, see the top of the file)
// ---------------------------------------------------------------------------

struct ScriptStep
{
   u64 buttons;
   int frames;
   bool exit;
};

std::vector<ScriptStep> g_script;
size_t g_script_pos = 0;
int g_script_left = 0;
u64 g_script_buttons = 0;
bool g_script_exit = false;

void load_pad_script()
{
   static const struct { const char *name; u64 bit; } kNames[] = {
      { "A", HidNpadButton_A }, { "B", HidNpadButton_B }, { "X", HidNpadButton_X }, { "Y", HidNpadButton_Y },
      { "L", HidNpadButton_L }, { "R", HidNpadButton_R }, { "ZL", HidNpadButton_ZL }, { "ZR", HidNpadButton_ZR },
      { "PLUS", HidNpadButton_Plus }, { "MINUS", HidNpadButton_Minus },
      { "UP", HidNpadButton_Up }, { "DOWN", HidNpadButton_Down },
      { "LEFT", HidNpadButton_Left }, { "RIGHT", HidNpadButton_Right }
   };
   FILE *f = fopen("padscript.txt", "r");
   if (!f)
      return;
   char line[128];
   while (fgets(line, sizeof(line), f)) {
      char what[96];
      int frames = 0;
      if (line[0] == '#' || sscanf(line, "%95s %d", what, &frames) != 2)
         continue;
      ScriptStep st;
      st.buttons = 0;
      st.frames = frames;
      st.exit = false;
      for (char *tok = strtok(what, "+"); tok; tok = strtok(NULL, "+")) {
         if (!strcmp(tok, "EXIT"))
            st.exit = true;
         for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
            if (!strcmp(tok, kNames[i].name))
               st.buttons |= kNames[i].bit;
      }
      g_script.push_back(st);
   }
   fclose(f);
   fprintf(stderr, "zplayer: pad script with %d steps\n", (int)g_script.size());
}

bool script_active()
{
   return !g_script.empty();
}

u64 script_step()
{
   while (g_script_left <= 0 && g_script_pos < g_script.size()) {
      const ScriptStep &st = g_script[g_script_pos++];
      g_script_left = st.frames;
      g_script_buttons = st.buttons;
      if (st.exit)
         g_script_exit = true;
   }
   if (g_script_left > 0)
      g_script_left--;
   else
      g_script_buttons = 0;
   return g_script_buttons;
}

// ---------------------------------------------------------------------------
// platform thread: input
// ---------------------------------------------------------------------------

PadState g_pad;
u64 g_ui_buttons = 0;
int g_keyboard_recent = 0;     // frames since A was pressed
bool g_keyboard_wanted = false;
bool g_touch_down = false;

void request_exit(const char *why)
{
   if (g.exit_requested.exchange(true))
      return;
   fprintf(stderr, "zplayer: exit requested (%s)\n", why);
   g.exit_request_us = now_us();
   zcport_close_button_pressed();
}

void poll_system()
{
   if (!g.exit_requested.load() && !appletMainLoop())
      request_exit("system");
   if (g_script_exit)
      request_exit("pad script");
}

// B is both ImGui's "cancel" and Escape: the dialog host closes on Escape.
void feed_ui_buttons(ImGuiIO &io, u64 now)
{
   static const struct { u64 bit; ImGuiKey key; } kMap[] = {
      { HidNpadButton_A, ImGuiKey_GamepadFaceDown },   // A: ImGui's "activate"
      { HidNpadButton_B, ImGuiKey_GamepadFaceRight },  // B: ImGui's "cancel"
      { HidNpadButton_B, ImGuiKey_Escape },
      { HidNpadButton_X, ImGuiKey_GamepadFaceUp },
      { HidNpadButton_Y, ImGuiKey_GamepadFaceLeft },
      { HidNpadButton_L, ImGuiKey_GamepadL1 },
      { HidNpadButton_R, ImGuiKey_GamepadR1 },
      { HidNpadButton_ZL, ImGuiKey_GamepadL2 },
      { HidNpadButton_ZR, ImGuiKey_GamepadR2 },
      { HidNpadButton_StickL, ImGuiKey_GamepadL3 },
      { HidNpadButton_StickR, ImGuiKey_GamepadR3 },
      { HidNpadButton_Plus, ImGuiKey_GamepadStart },
      { HidNpadButton_Minus, ImGuiKey_GamepadBack },
      { HidNpadButton_Up, ImGuiKey_GamepadDpadUp },
      { HidNpadButton_Down, ImGuiKey_GamepadDpadDown },
      { HidNpadButton_Left, ImGuiKey_GamepadDpadLeft },
      { HidNpadButton_Right, ImGuiKey_GamepadDpadRight },
   };
   const u64 changed = now ^ g_ui_buttons;
   for (size_t i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++)
      if (changed & kMap[i].bit)
         io.AddKeyEvent(kMap[i].key, (now & kMap[i].bit) != 0);
   if ((now & ~g_ui_buttons) & HidNpadButton_A)
      g_keyboard_recent = 4;
   g_ui_buttons = now;
}

void feed_ui_stick(ImGuiIO &io, const HidAnalogStickState &l)
{
   const float kDead = 0.20f;
   const float x = l.x / 32767.0f, y = l.y / 32767.0f;
   io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, x < -kDead, x < -kDead ? -x : 0.0f);
   io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, x > kDead, x > kDead ? x : 0.0f);
   io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, y > kDead, y > kDead ? y : 0.0f);
   io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, y < -kDead, y < -kDead ? -y : 0.0f);
}

// The touch screen is the UI's mouse (coordinates are 1280 x 720 already).
void feed_ui_touch(ImGuiIO &io)
{
   HidTouchScreenState st;
   memset(&st, 0, sizeof(st));
   if (!hidGetTouchScreenStates(&st, 1))
      return;
   const bool down = st.count > 0;
   if (down)
      io.AddMousePosEvent((float)st.touches[0].x, (float)st.touches[0].y);
   if (down != g_touch_down) {
      io.AddMouseButtonEvent(0, down);
      g_touch_down = down;
   }
}

// A text field the player just activated with A: the system keyboard. It takes
// over the screen until it returns; the game thread only waits for UI frames.
void maybe_show_keyboard(ImGuiIO &io)
{
   const bool want = io.WantTextInput;
   const bool activate = want && !g_keyboard_wanted && g_keyboard_recent > 0 && !script_active();
   g_keyboard_wanted = want;
   if (g_keyboard_recent > 0)
      g_keyboard_recent--;
   if (!activate)
      return;

   ImGuiContext &ctx = *ImGui::GetCurrentContext();
   ImGuiInputTextState *field = ImGui::GetInputTextState(ctx.ActiveId);
   SwkbdConfig kbd;
   if (R_FAILED(swkbdCreate(&kbd, 0)))
      return;
   char out[512];
   out[0] = 0;
   swkbdConfigMakePresetDefault(&kbd);
   if (field && field->TextA.Data)
      swkbdConfigSetInitialText(&kbd, field->TextA.Data);
   Result rc = swkbdShow(&kbd, out, sizeof(out));
   swkbdClose(&kbd);
   if (R_SUCCEEDED(rc)) {
      io.AddKeyEvent(ImGuiMod_Ctrl, true);
      io.AddKeyEvent(ImGuiKey_A, true);
      io.AddKeyEvent(ImGuiKey_A, false);
      io.AddKeyEvent(ImGuiMod_Ctrl, false);
      io.AddInputCharactersUTF8(out);
      io.AddKeyEvent(ImGuiKey_Enter, true);
      io.AddKeyEvent(ImGuiKey_Enter, false);
   }
   else {
      io.AddKeyEvent(ImGuiKey_Escape, true);
      io.AddKeyEvent(ImGuiKey_Escape, false);
   }
}

void poll_input(ImGuiIO &io)
{
   u64 raw = 0;
   HidAnalogStickState sticks[2];
   memset(sticks, 0, sizeof(sticks));
   if (script_active()) {
      raw = script_step();
   }
   else {
      padUpdate(&g_pad);
      raw = padGetButtons(&g_pad);
      sticks[0] = padGetStickPos(&g_pad, 0);
      sticks[1] = padGetStickPos(&g_pad, 1);
      feed_ui_touch(io);
   }
   feed_ui_buttons(io, raw);
   feed_ui_stick(io, sticks[0]);

   // A session's input is the UI's alone; releases still reach the game.
   std::lock_guard<std::mutex> guard(g.input_mutex);
   if (zcui::session_active() && zcui::available()) {
      g.buttons &= raw;
      memset(g.sticks, 0, sizeof(g.sticks));
   }
   else {
      g.buttons = raw;
      g.sticks[0] = sticks[0];
      g.sticks[1] = sticks[1];
   }
}

// ---------------------------------------------------------------------------
// platform thread: the main loop
// ---------------------------------------------------------------------------

// Takes the newest frame the game handed over. If there has been none for a
// while (a busy loop, a long load), copies the screen.
void take_frame()
{
   bool have = false;
   {
      std::lock_guard<std::mutex> guard(g.frame_mutex);
      if (g.pending_new) {
         std::swap(g.shown.px, g.pending.px);
         g.shown.w = g.pending.w;
         g.shown.h = g.pending.h;
         g.pending_new = false;
         have = true;
      }
      else if (g.display && g.mode_active.load() &&
               now_us() - g.last_handover_us.load() > 100000) {
         BITMAP *d = g.display;
         uint32_t lut[256];
         build_lut(_current_palette, lut);
         g.shown.px.resize((size_t)d->w * d->h);
         g.shown.w = d->w;
         g.shown.h = d->h;
         convert_bitmap(d, lut, &g.shown.px[0]);
         have = true;
      }
   }
   if (have)
      zcgles::set_picture(&g.shown.px[0], g.shown.w, g.shown.h);
}

void platform_thread_main(void *arg)
{
   (void)arg;
   NWindow *win = nwindowGetDefault();
   nwindowSetDimensions(win, kFbWidth, kFbHeight);

   std::string err;
   bool ok = zcgles::init(win, kFbWidth, kFbHeight, &err);
   if (ok) {
      padConfigureInput(1, HidNpadStyleSet_NpadStandard);
      padInitializeDefault(&g_pad);
      hidInitializeTouchScreen();
      load_pad_script();
      if (zcui_platform::attach()) {
         ImGuiIO &io = ImGui::GetIO();
         io.BackendPlatformName = "libnx";
         io.BackendRendererName = "OpenGL ES 3";
         io.BackendFlags |= ImGuiBackendFlags_HasGamepad | ImGuiBackendFlags_RendererHasTextures;
         io.ConfigNavMoveSetMousePos = false;
         io.ConfigNavCursorVisibleAlways = true;   // no mouse: the first A already presses
         zcui_platform::set_renderer_ready(true);
      }
      else {
         ok = false;
         err = "the UI layer could not start";
      }
   }
   {
      std::lock_guard<std::mutex> guard(g.init_mutex);
      g.init_done = true;
      g.init_ok = ok;
      g.init_error = err;
   }
   g.init_cv.notify_all();
   if (!ok) {
      zcgles::shutdown();
      return;
   }

   uint64_t last = now_us(), last_stat = last;
   unsigned presents = 0;
   while (!g.quit.load()) {
      poll_system();
      ImGuiIO &io = ImGui::GetIO();
      poll_input(io);
      maybe_show_keyboard(io);
      take_frame();

      const uint64_t now = now_us();
      io.DisplaySize = ImVec2((float)kFbWidth, (float)kFbHeight);
      io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
      io.DeltaTime = now > last ? (float)(now - last) / 1000000.0f : 1.0f / kRefreshHz;
      last = now;

      ImDrawData *ui = zcui_platform::build_frame();
      const zcdisplay::Settings s = display_get();
      int x = 0, y = 0, dw = 0, dh = 0;
      if (g.mode_active.load())
         picture_rect(g.shown.w, g.shown.h, s.scaling, &x, &y, &dw, &dh);
      zcgles::present(ui, x, y, dw, dh, s.scaling == zcdisplay::SCALING_FIT_SMOOTH);

      presents++;
      if (now - last_stat >= 5000000) {
         const float secs = (float)(now - last_stat) / 1000000.0f;
         fprintf(stderr, "zplayer: %.1f frames shown/s, %.1f from the game/s\n",
                 presents / secs, g.handovers.exchange(0) / secs);
         presents = 0;
         last_stat = now;
      }
   }

   zcui_platform::set_renderer_ready(false);
   zcui_platform::shutdown();
   zcgles::shutdown();
}

bool start_platform(std::string *error)
{
   if (g.running)
      return true;
   g.quit = false;
   g.init_done = false;
   g.init_ok = false;
   u64 mask = 0;
   svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
   // A core of its own when there is one; the game stays on the default core.
   const int core = (mask & 2) ? 1 : -2;
   Result rc = threadCreate(&g.thread, platform_thread_main, NULL, NULL, 0x200000, 0x2C, core);
   if (R_SUCCEEDED(rc)) {
      rc = threadStart(&g.thread);
      if (R_FAILED(rc))
         threadClose(&g.thread);
   }
   if (R_FAILED(rc)) {
      char buf[64];
      snprintf(buf, sizeof(buf), "cannot start the platform thread (0x%x)", (unsigned)rc);
      *error = buf;
      return false;
   }
   std::unique_lock<std::mutex> lock(g.init_mutex);
   g.init_cv.wait(lock, [] { return g.init_done; });
   if (!g.init_ok) {
      *error = g.init_error;
      lock.unlock();
      threadWaitForExit(&g.thread);
      threadClose(&g.thread);
      return false;
   }
   g.running = true;
   return true;
}

void stop_platform()
{
   if (!g.running)
      return;
   g.quit = true;
   threadWaitForExit(&g.thread);
   threadClose(&g.thread);
   g.running = false;
}

// ---------------------------------------------------------------------------
// audio
// ---------------------------------------------------------------------------

const int kAudioFrames = 1024;        // one page per block (audout's buffer alignment)
const int kAudioBuffers = 3;
const int kAudioBlockBytes = kAudioFrames * 4;
const size_t kAudioBlockAligned = 0x1000;

struct Audio
{
   Thread thread;
   std::atomic<bool> run;
   bool open;
   ZCPORT_AUDIO_MIX_FUNC mix;
   void *user;
   void *memory;
   AudioOutBuffer buffers[kAudioBuffers];
};

Audio g_audio;

void audio_fill_and_queue(AudioOutBuffer *b)
{
   g_audio.mix(g_audio.user, b->buffer, kAudioBlockBytes);
   armDCacheFlush(b->buffer, kAudioBlockBytes);
   audoutAppendAudioOutBuffer(b);
}

void audio_thread_main(void *arg)
{
   (void)arg;
   for (int i = 0; i < kAudioBuffers; i++)
      audio_fill_and_queue(&g_audio.buffers[i]);
   while (g_audio.run.load()) {
      AudioOutBuffer *released = NULL;
      u32 count = 0;
      Result rc = audoutWaitPlayFinish(&released, &count, 100000000ULL);   // 100 ms
      if (R_FAILED(rc) || !released || !count) {
         svcSleepThread(1000000);
         continue;
      }
      audio_fill_and_queue(released);
   }
}

void audio_stop()
{
   if (!g_audio.open)
      return;
   g_audio.run.store(false);
   threadWaitForExit(&g_audio.thread);
   threadClose(&g_audio.thread);
   audoutStopAudioOut();
   audoutExit();
   free(g_audio.memory);
   g_audio.memory = NULL;
   g_audio.mix = NULL;
   g_audio.user = NULL;
   g_audio.open = false;
}

int hook_audio_open(int freq, int channels, int bits, ZCPORT_AUDIO_MIX_FUNC mix, void *user, int *out_freq)
{
   (void)freq;
   if (!mix || channels != 2 || bits != 16 || g_audio.open)
      return -1;

   Result rc = audoutInitialize();
   if (R_FAILED(rc)) {
      fprintf(stderr, "zplayer: audoutInitialize failed (0x%x), sound is silent\n", (unsigned)rc);
      return -1;
   }
   rc = audoutStartAudioOut();
   if (R_FAILED(rc)) {
      fprintf(stderr, "zplayer: audoutStartAudioOut failed (0x%x), sound is silent\n", (unsigned)rc);
      audoutExit();
      return -1;
   }

   g_audio.memory = memalign(0x1000, kAudioBlockAligned * kAudioBuffers);
   if (!g_audio.memory) {
      audoutStopAudioOut();
      audoutExit();
      return -1;
   }
   memset(g_audio.memory, 0, kAudioBlockAligned * kAudioBuffers);
   for (int i = 0; i < kAudioBuffers; i++) {
      AudioOutBuffer &b = g_audio.buffers[i];
      b.next = NULL;
      b.buffer = (char *)g_audio.memory + kAudioBlockAligned * i;
      b.buffer_size = kAudioBlockAligned;
      b.data_size = kAudioBlockBytes;
      b.data_offset = 0;
   }

   g_audio.mix = mix;
   g_audio.user = user;
   g_audio.run.store(true);

   // Above the game thread (0x2C), so a busy frame does not starve the device.
   u64 mask = 0;
   svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
   rc = threadCreate(&g_audio.thread, audio_thread_main, NULL, NULL, 0x80000, 0x2B, (mask & 4) ? 2 : -2);
   if (R_SUCCEEDED(rc)) {
      rc = threadStart(&g_audio.thread);
      if (R_FAILED(rc))
         threadClose(&g_audio.thread);
   }
   if (R_FAILED(rc)) {
      fprintf(stderr, "zplayer: cannot start the audio thread (0x%x), sound is silent\n", (unsigned)rc);
      g_audio.run.store(false);
      audoutStopAudioOut();
      audoutExit();
      free(g_audio.memory);
      g_audio.memory = NULL;
      g_audio.mix = NULL;
      return -1;
   }
   g_audio.open = true;
   if (out_freq)
      *out_freq = (int)audoutGetSampleRate();   // 48000; the mixer follows it
   return 0;
}

void hook_audio_close(void)
{
   audio_stop();
}

// ---------------------------------------------------------------------------
// hooks (game thread unless noted)
// ---------------------------------------------------------------------------

int hook_init(void)
{
   std::string err;
   if (!start_platform(&err)) {
      fprintf(stderr, "zplayer: %s\n", err.c_str());
      zcport_set_error(err.c_str());
      return -1;
   }
   return 0;
}

void hook_exit(void)
{
   audio_stop();
   stop_platform();
   g.mode_active = false;
   std::lock_guard<std::mutex> guard(g.frame_mutex);
   g.display = NULL;
}

void hand_over_frame(BITMAP *display, const RGB *palette)
{
   if (!display || display->w <= 0 || display->h <= 0)
      return;
   uint32_t lut[256];
   build_lut(palette ? palette : _current_palette, lut);
   Frame &f = g.work;
   f.w = display->w;
   f.h = display->h;
   f.px.resize((size_t)f.w * f.h);
   convert_bitmap(display, lut, &f.px[0]);
   {
      std::lock_guard<std::mutex> guard(g.frame_mutex);
      std::swap(g.pending.px, f.px);
      g.pending.w = f.w;
      g.pending.h = f.h;
      g.pending_new = true;
      g.display = display;
   }
   g.last_handover_us = now_us();
   g.handovers++;
}

void hook_pump_events(void)
{
   if (!g.running)
      return;
   // The game saves and exits when it next looks at its close button; if it
   // is somewhere that never does, do the same from here before libnx's
   // 15 seconds run out.
   if (g.exit_requested.load() && now_us() - g.exit_request_us.load() > kExitGraceUs) {
      fprintf(stderr, "zplayer: the game did not exit in time, saving\n");
      zplayer_switch_final_save();
      exit(0);
   }
   if (!g.mode_active.load() || !zcport_display)
      return;
   if (now_us() - g.last_handover_us.load() + 1500 < kFramePeriodUs)
      return;
   hand_over_frame(zcport_display, _current_palette);
}

uint64_t hook_get_ticks_us(void)
{
   return now_us();
}

void hook_sleep_ms(unsigned int ms)
{
   svcSleepThread((s64)ms * 1000000);
}

std::string g_message;
std::atomic<bool> g_message_done(false);

void message_layer(void *)
{
   ImGuiIO &io = ImGui::GetIO();
   const float sc = zcui::scale();
   ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                           ImVec2(0.5f, 0.5f));
   ImGui::SetNextWindowSize(ImVec2(560 * sc, 0), ImGuiCond_Always);
   ImGui::SetNextWindowFocus();
   if (ImGui::Begin("ZC Vulkan", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
      ImGui::TextWrapped("%s", g_message.c_str());
      ImGui::Spacing();
      if (ImGui::Button("OK", ImVec2(120 * sc, 0)) ||
          ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
          ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
         g_message_done = true;
   }
   ImGui::End();
}

// allegro_message(): shown on screen, waits for A or B. It can come before
// install_allegro() or after allegro_exit(), so the platform starts for it.
void hook_message(const char *msg)
{
   fprintf(stderr, "%s\n", msg ? msg : "");
   fflush(stderr);
   std::string err;
   const bool started_here = !g.running;
   if (!start_platform(&err) || !zcui::available())
      return;
   g_message = msg ? msg : "";
   g_message_done = false;
   const int layer = zcui::add_layer(message_layer, NULL, 2000);
   zcui::begin_session();
   while (!g_message_done.load() && !g.exit_requested.load())
      zcui::wait_frame(100);
   zcui::end_session();
   zcui::remove_layer(layer);
   if (started_here)
      stop_platform();
}

int hook_get_desktop_resolution(int *w, int *h)
{
   if (w) *w = kFbWidth;
   if (h) *h = kFbHeight;
   return 0;
}

int hook_desktop_color_depth(void)
{
   return 32;
}

int hook_get_executable_name(char *out, int size)
{
   if (!out || size <= 0)
      return -1;
   strncpy(out, kExecutableName, (size_t)size - 1);
   out[size - 1] = 0;
   return 0;
}

int hook_gfx_open(int w, int h, int color_depth, int windowed)
{
   (void)color_depth;
   if (w <= 0 || h <= 0) {
      zcport_set_error("Unsupported screen size");
      return -1;
   }
   std::string err;
   if (!start_platform(&err)) {
      zcport_set_error(err.c_str());
      return -1;
   }
   display_before_open();
   {
      std::lock_guard<std::mutex> guard(g.frame_mutex);
      g.display = NULL;             // the game is about to replace the screen
      g.pending_new = false;
   }
   g.disp_w = w;
   g.disp_h = h;
   g.windowed = windowed > 0;
   g.mode_active = true;
   g.last_handover_us = 0;
   return 0;
}

void hook_gfx_close(void)
{
   std::lock_guard<std::mutex> guard(g.frame_mutex);
   g.display = NULL;
   g.pending_new = false;
   g.mode_active = false;
}

void hook_gfx_present(BITMAP *display, const RGB *palette)
{
   if (now_us() - g.last_handover_us.load() + 1500 >= kFramePeriodUs)
      hand_over_frame(display, palette);
}

// vsync(): wait for the next 60 Hz tick.
void hook_gfx_vsync(void)
{
   const uint64_t t = now_us();
   const uint64_t next = (t / kFramePeriodUs + 1) * kFramePeriodUs;
   svcSleepThread((s64)((next - t) * 1000));
}

int hook_gfx_is_windowed(void)
{
   return g.windowed.load() ? 1 : 0;
}

int hook_gfx_refresh_rate(void)
{
   return kRefreshHz;
}

int hook_joy_count(void)
{
   // Always one, so the game's joystick_index stays valid while a controller
   // is being attached (its state then reads as released).
   return 1;
}

inline int stick_axis(int v)
{
   return std::max(-32768, std::min(32767, v));
}

// The order makes the game's default bindings (zc.cfg, btn_a = 2, btn_b = 1,
// btn_s = 10, btn_m = 9, btn_l = 5, btn_r = 6, btn_p = 12, btn_ex1..4 = 7, 8,
// 4, 3, btn_up/down/left/right = 13..16, all counted from 1) land on the
// matching Switch buttons.
int hook_joy_poll(int index, ZCPORT_JOY_STATE *state)
{
   if (index != 0 || !state)
      return -1;
   static const u64 kButtons[16] = {
      HidNpadButton_B,    HidNpadButton_A,     HidNpadButton_Y,     HidNpadButton_X,
      HidNpadButton_L,    HidNpadButton_R,     HidNpadButton_ZL,    HidNpadButton_ZR,
      HidNpadButton_Minus, HidNpadButton_Plus, HidNpadButton_StickR, HidNpadButton_StickL,
      HidNpadButton_Up,   HidNpadButton_Down,  HidNpadButton_Left,  HidNpadButton_Right
   };
   u64 b;
   HidAnalogStickState sticks[2];
   {
      std::lock_guard<std::mutex> guard(g.input_mutex);
      b = g.buttons;
      sticks[0] = g.sticks[0];
      sticks[1] = g.sticks[1];
   }
   memset(state, 0, sizeof(*state));
   state->name = "Switch Controller";

   state->num_buttons = 16;
   for (int i = 0; i < 16; i++)
      state->button[i] = (b & kButtons[i]) ? 1 : 0;

   // Left stick, right stick (Allegro: up is negative). The D-pad also moves
   // the left stick: the game's default (analog_movement = 1) only reads
   // stick 0 for movement, and the D-pad is what Zelda is played with.
   int lx = sticks[0].x, ly = -sticks[0].y;
   if (b & HidNpadButton_Left)
      lx = -32768;
   else if (b & HidNpadButton_Right)
      lx = 32767;
   if (b & HidNpadButton_Up)
      ly = -32768;
   else if (b & HidNpadButton_Down)
      ly = 32767;
   state->num_axes = 4;
   state->axis[0] = stick_axis(lx);
   state->axis[1] = stick_axis(ly);
   state->axis[2] = stick_axis(sticks[1].x);
   state->axis[3] = stick_axis(-sticks[1].y);

   state->num_hats = 1;
   int hat = 0;
   if (b & HidNpadButton_Up)    hat |= 1;
   if (b & HidNpadButton_Right) hat |= 2;
   if (b & HidNpadButton_Down)  hat |= 4;
   if (b & HidNpadButton_Left)  hat |= 8;
   state->hat[0] = hat;
   return 0;
}

ZCPORT_PLATFORM make_table()
{
   ZCPORT_PLATFORM t;
   memset(&t, 0, sizeof(t));
   t.version = ZCPORT_PLATFORM_VERSION;
   t.name = "libnx";
   t.init = hook_init;
   t.exit = hook_exit;
   t.pump_events = hook_pump_events;
   t.get_ticks_us = hook_get_ticks_us;
   t.sleep_ms = hook_sleep_ms;
   t.message = hook_message;
   t.get_desktop_resolution = hook_get_desktop_resolution;
   t.desktop_color_depth = hook_desktop_color_depth;
   t.get_executable_name = hook_get_executable_name;
   t.gfx_open = hook_gfx_open;
   t.gfx_close = hook_gfx_close;
   t.gfx_present = hook_gfx_present;
   t.gfx_vsync = hook_gfx_vsync;
   t.gfx_is_windowed = hook_gfx_is_windowed;
   t.gfx_refresh_rate = hook_gfx_refresh_rate;
   t.joy_count = hook_joy_count;
   t.joy_poll = hook_joy_poll;
   t.audio_open = hook_audio_open;
   t.audio_close = hook_audio_close;
   // set_window_title, native_window, mouse_warp, mouse_show_os_cursor,
   // audio_lock/unlock and close_button_enable stay NULL.
   return t;
}

ZCPORT_PLATFORM g_table = make_table();

} // namespace

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------

extern "C" void zcport_sdl_register(void)
{
   if (zcport_get_platform() != &g_table)
      zcport_set_platform(&g_table);
   if (!zcport_get_midi_backend())
      zcport_set_midi_backend(zcport_sdl::midi_backend());
}

// There is one screen.
extern "C" void zcport_place_window_on_display(int display)
{
   (void)display;
}

namespace zcdisplay
{

Settings defaults() { return display_defaults(); }

std::vector<Monitor> monitors()
{
   Monitor m;
   m.index = 1;
   m.name = "Switch";
   m.x = m.y = 0;
   m.w = m.desktop_w = m.max_client_w = kFbWidth;
   m.h = m.desktop_h = m.max_client_h = kFbHeight;
   m.desktop_hz = (float)kRefreshHz;
   m.primary = true;
   return std::vector<Monitor>(1, m);
}

std::vector<DisplayMode> fullscreen_modes(int display)
{
   (void)display;
   DisplayMode m;
   m.w = kFbWidth;
   m.h = kFbHeight;
   m.hz = (float)kRefreshHz;
   return std::vector<DisplayMode>(1, m);
}

int window_monitor() { return 1; }
unsigned monitors_serial() { return 0; }
Settings get() { return display_get(); }
void set(const Settings &s) { display_set(s); }
void toggle_fullscreen() {}
bool is_fullscreen() { return true; }
bool fullscreen_changed() { return false; }
unsigned hotkey_toggles() { return 0; }
bool managed() { return true; }

void logical_size(int *w, int *h)
{
   const bool on = g.mode_active.load();
   if (w) *w = on ? g.disp_w.load() : 0;
   if (h) *h = on ? g.disp_h.load() : 0;
}

int refresh_hz() { return kRefreshHz; }
std::string renderer_in_use() { return "OpenGL ES 3"; }
int renderer_at_start() { return RENDERER_AUTO; }
void set_settings_loader(LoadSettingsFn fn) { g_settings_loader = fn; }

} // namespace zcdisplay

#endif // __SWITCH__

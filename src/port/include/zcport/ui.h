/*
 * zplayer's user interface layer: Dear ImGui, drawn by the Vulkan presenter
 * over the game picture at the window's own resolution (menus, dialogs,
 * settings). It replaces jwin in zplayer.
 *
 * Threads. ImGui runs on the platform thread, which owns the window and the
 * presenter: it feeds SDL events to ImGui and builds one UI frame for every
 * frame it shows. Game-side UI code registers "layers": functions the
 * platform thread calls between ImGui::NewFrame() and ImGui::Render(), with
 * the UI lock held. Game-thread code that shares state with a layer takes the
 * same lock (zcui::Lock) while it reads or changes that state.
 *
 * Sessions. While at least one session is open (a menu or dialog is up), the
 * platform redraws continuously and keyboard, mouse and gamepad input go to
 * the UI only; the game sees no key or button changes. Sessions nest.
 *
 * Typical modal use on the game thread:
 *
 *     zcui::begin_session();
 *     while(!done)
 *         zcui::wait_frame(100);      // the layer sets 'done' (under the lock)
 *     zcui::end_session();
 *
 * C++ only. Include after <allegro.h> if both are needed; it includes
 * neither SDL nor Vulkan, only <imgui.h> is needed by layer code.
 */

#ifndef ZCPORT_UI_H
#define ZCPORT_UI_H

namespace zcui
{

typedef void (*LayerFn)(void *user);

/* Register a layer; layers run in ascending 'order' every UI frame.
 * Returns an id for remove_layer(). Thread-safe. */
int  add_layer(LayerFn fn, void *user, int order);
void remove_layer(int id);

/* The UI lock: layers run with it held. Recursive. */
void lock();
void unlock();

struct Lock
{
    Lock() { lock(); }
    ~Lock() { unlock(); }
private:
    Lock(const Lock &);
    Lock &operator=(const Lock &);
};

/* Modal sessions (see above). Thread-safe. */
void begin_session();
void end_session();
bool session_active();

/* Game thread: wait until the platform has built the next UI frame (or
 * 'timeout_ms' passed). Returns true if a frame was built. Must not be called
 * with the UI lock held. */
bool wait_frame(int timeout_ms);

/* Ask the platform to show a new frame soon (state changed). */
void request_redraw();

/* True when UI frames can be shown (the Vulkan presenter is running). When
 * false, layers are never called; modal code must not wait for them. */
bool available();

/* UI scale (1.0 = 96 dpi); fonts and sizes are already scaled by it. It is
 * the display's scale times the user's factor below. */
float scale();

/* The user's UI scale factor (Video Settings, 1.0 = 100 %), multiplied
 * with the display's own scale. Takes effect with the next UI frame.
 * Thread-safe. */
void set_user_scale(float factor);
float user_scale();

/* While no session is open: stop this kind of input at the UI, so that the
 * game does not see it (an overlay such as a menu bar over a running editor).
 * Called from layers, every frame. */
void set_capture(bool mouse, bool keys);

} // namespace zcui

#endif /* ZCPORT_UI_H */

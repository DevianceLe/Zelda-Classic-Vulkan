// zqeditor: a test driver. With the environment variable ZQ_UI_SCRIPT set to a
// file name, commands appended to that file are fed to the program as SDL
// input events, so a dialog or page can be exercised without taking the
// keyboard focus or moving the real mouse. Unset, this does nothing.
//
// Commands, one per line (coordinates are window pixels):
//   move X Y | click X Y [HOLD_MS] | rclick X Y | dclick X Y | wait MS | key NAME [MS] | type TEXT
// The number of commands handled is written to <file>.pos after each one.
//
// Written from scratch for zcport.

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

extern "C" int set_display_switch_mode(int mode);

namespace
{

SDL_WindowID window_id()
{
    int n = 0;
    SDL_Window **list = SDL_GetWindows(&n);
    SDL_WindowID id = (list && n > 0) ? SDL_GetWindowID(list[0]) : 0;
    SDL_free(list);
    return id;
}

// On the thread that owns the window: SDL's window calls are not safe elsewhere.
void SDLCALL make_unfocusable(void *)
{
    int n = 0;
    SDL_Window **list = SDL_GetWindows(&n);
    if(list && n > 0)
        SDL_SetWindowFocusable(list[0], false);
    SDL_free(list);
}

// The driver thread runs until the program ends. It reads its file through the
// C++ runtime's streams, and exit() frees their locale data after the program's
// own static objects are gone: a read at that moment called through freed
// memory and crashed the program as it closed (about 1 run in 150 under the
// test tools). ~Start stops the thread before that.
std::atomic<bool> g_stop(false);
std::atomic<bool> g_running(false);

// In short steps, so the thread notices g_stop.
void pause_ms(int ms)
{
    for(int left = ms; left > 0 && !g_stop; left -= 10)
        std::this_thread::sleep_for(std::chrono::milliseconds(left < 10 ? left : 10));
}

void motion(float x, float y)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = window_id();
    e.motion.x = x;
    e.motion.y = y;
    SDL_PushEvent(&e);
    pause_ms(70);
}

void button(int which, bool down, float x, float y)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = window_id();
    e.button.button = (Uint8)which;
    e.button.down = down;
    e.button.clicks = 1;
    e.button.x = x;
    e.button.y = y;
    SDL_PushEvent(&e);
    pause_ms(70);
}

// hold: how long the button stays down. The editor polls the mouse once a frame, and while
// it is busy redrawing a 70 ms press can fall between two polls.
void click(int which, float x, float y, int hold = 0)
{
    motion(x, y);
    button(which, true, x, y);
    if(hold > 0)
        pause_ms(hold);
    button(which, false, x, y);
}

SDL_Scancode scancode_of(const std::string &name)
{
    if(name == "ESC") return SDL_SCANCODE_ESCAPE;
    if(name == "ENTER") return SDL_SCANCODE_RETURN;
    if(name == "SPACE") return SDL_SCANCODE_SPACE;
    if(name == "TAB") return SDL_SCANCODE_TAB;
    if(name == "UP") return SDL_SCANCODE_UP;
    if(name == "DOWN") return SDL_SCANCODE_DOWN;
    if(name == "LEFT") return SDL_SCANCODE_LEFT;
    if(name == "RIGHT") return SDL_SCANCODE_RIGHT;
    if(name == "PGUP") return SDL_SCANCODE_PAGEUP;
    if(name == "PGDN") return SDL_SCANCODE_PAGEDOWN;
    if(name == "BKSP") return SDL_SCANCODE_BACKSPACE;
    if(name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z')
        return (SDL_Scancode)(SDL_SCANCODE_A + (name[0] - 'A'));
    if(name.size() == 1 && name[0] >= '1' && name[0] <= '9')
        return (SDL_Scancode)(SDL_SCANCODE_1 + (name[0] - '1'));
    if(name.size() == 2 && name[0] == 'F' && name[1] >= '1' && name[1] <= '9')
        return (SDL_Scancode)(SDL_SCANCODE_F1 + (name[1] - '1'));
    return SDL_SCANCODE_UNKNOWN;
}

void key(const std::string &name, int ms)
{
    SDL_Scancode sc = scancode_of(name);
    if(sc == SDL_SCANCODE_UNKNOWN)
        return;
    for(int down = 1; down >= 0; --down)
    {
        SDL_Event e;
        SDL_zero(e);
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.windowID = window_id();
        e.key.scancode = sc;
        e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
        e.key.down = down != 0;
        SDL_PushEvent(&e);
        // as the system does: a letter is also typed text, if text input is on
        SDL_WindowID id = window_id();
        SDL_Window *win = SDL_GetWindowFromID(id);
        if(down && win && SDL_TextInputActive(win) && name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z')
        {
            char letter[2] = { (char)(name[0] - 'A' + 'a'), 0 };
            SDL_Event t;
            SDL_zero(t);
            t.type = SDL_EVENT_TEXT_INPUT;
            t.text.windowID = id;
            t.text.text = SDL_strdup(letter);
            SDL_PushEvent(&t);
        }
        pause_ms(down ? ms : 60);
    }
}

void run(const std::string &path)
{
    std::streamoff at = 0;
    int handled = 0;
    bool unfocusable = false;
    while(!g_stop)
    {
        // A window under test must never take the keyboard from whoever is
        // using the computer, and the UI reads the real mouse when focused.
        if(!unfocusable)
        {
            int n = 0;
            SDL_Window **list = SDL_GetWindows(&n);
            if(list && n > 0)
            {
                SDL_RunOnMainThread(make_unfocusable, NULL, true);
                unfocusable = true;
                // Until the editor has started it pauses without the focus,
                // and a window under test never has it. (3: SWITCH_BACKGROUND)
                set_display_switch_mode(3);
            }
            SDL_free(list);
        }
        std::ifstream f(path.c_str());
        if(f)
        {
            f.seekg(at);
            std::string line;
            while(!g_stop && std::getline(f, line))
            {
                if(f.eof())
                    break;          // the line is not complete yet
                at = f.tellg();
                char word[32] = "", name[32] = "";
                int a = 0, b = 0, c = 0;
                if(sscanf(line.c_str(), "%31s %d %d %d", word, &a, &b, &c) >= 1)
                {
                    float x = (float)a, y = (float)b;
                    if(!strcmp(word, "move")) motion(x, y);
                    else if(!strcmp(word, "click")) click(SDL_BUTTON_LEFT, x, y, c);
                    else if(!strcmp(word, "rclick")) click(SDL_BUTTON_RIGHT, x, y);
                    else if(!strcmp(word, "dclick")) { click(SDL_BUTTON_LEFT, x, y); click(SDL_BUTTON_LEFT, x, y); }
                    else if(!strcmp(word, "wait")) pause_ms(a);
                    else if(!strcmp(word, "type"))
                    {
                        // text as the system's text input delivers it
                        SDL_Event e;
                        SDL_zero(e);
                        e.type = SDL_EVENT_TEXT_INPUT;
                        e.text.windowID = window_id();
                        e.text.text = SDL_strdup(line.c_str() + 5);
                        SDL_PushEvent(&e);
                        pause_ms(100);
                    }
                    else if(!strcmp(word, "key"))
                    {
                        int ms = 80;
                        sscanf(line.c_str(), "%31s %31s %d", word, name, &ms);
                        key(name, ms);
                    }
                }
                ++handled;
                std::ofstream pos((path + ".pos").c_str(), std::ios::trunc);
                pos << handled << std::endl;
            }
        }
        pause_ms(40);
    }
    g_running = false;
}

struct Start
{
    Start()
    {
        const char *e = getenv("ZQ_UI_SCRIPT");
        if(e && e[0])
        {
            std::string path(e);
            g_running = true;
            std::thread(run, path).detach();
        }
    }

    // Static objects are destroyed in the reverse order of their construction,
    // so this runs before the C++ runtime's own clean-up.
    ~Start()
    {
        g_stop = true;
        for(int i = 0; i < 300 && g_running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
} g_start;

} // namespace

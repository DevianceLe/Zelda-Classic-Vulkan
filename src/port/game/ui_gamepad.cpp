// zplayer: Settings > Controls > Gamepad Setup... (Dear ImGui), see zcport/gamepad_setup.h.
//
//   - The window is a layer (platform thread, UI lock held) shown during a modal UI session
//     that zplayer_gamepad_setup() holds open on the game thread, as Video Settings does.
//   - It lists the connected pads. A pad SDL knows reads in the standard layout
//     (platform_sdl.cpp refresh_joy) and the window shows which of its buttons are down, with
//     the game action each one has. A pad SDL doesn't know can be set up: the window asks for
//     each button in turn, reading the device's raw buttons, hats and axes, and writes an SDL
//     gamepad mapping (zcport_sdl_gamepad_add_mapping), after which the platform opens it as a
//     gamepad in the standard layout, for the game and the menus alike. A known pad that reads
//     wrong can be set up the same way; the new mapping replaces SDL's.
//
// Written from scratch for zplayer.

#if !defined(__SWITCH__)

#include <allegro.h>

#include "zcport/gamepad_setup.h"
#include "zcport/platform_sdl.h"
#include "zcport/ui.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// The game's joystick buttons (src/zelda.cpp; 1-based, 0 = none).
extern int Abtn, Bbtn, Sbtn, Mbtn, Lbtn, Rbtn, Pbtn, Exbtn1, Exbtn2, Exbtn3, Exbtn4;

namespace
{

// The standard layout's buttons, 1-based as the game counts them (platform_sdl.cpp).
const char *const kStdButtons[18] = {
    "Bottom face", "Right face", "Left face", "Top face", "L / LB", "R / RB", "L2 / LT", "R2 / RT",
    "Select / Back", "Start", "R3 (right stick)", "L3 (left stick)", "D-pad Up", "D-pad Down",
    "D-pad Left", "D-pad Right", "Home / Guide", "Share / Capture"
};

enum { K_BUTTON, K_STICK, K_TRIGGER };

struct Prompt
{
    const char *field;      // SDL's mapping name
    const char *label;
    int kind;
};

const Prompt kPrompts[] = {
    { "dpup", "D-pad Up", K_BUTTON },
    { "dpdown", "D-pad Down", K_BUTTON },
    { "dpleft", "D-pad Left", K_BUTTON },
    { "dpright", "D-pad Right", K_BUTTON },
    { "a", "the bottom face button (Cross on PlayStation, A on Xbox)", K_BUTTON },
    { "b", "the right face button (Circle, B)", K_BUTTON },
    { "x", "the left face button (Square, X)", K_BUTTON },
    { "y", "the top face button (Triangle, Y)", K_BUTTON },
    { "back", "Select (Back, Minus)", K_BUTTON },
    { "start", "Start (Plus)", K_BUTTON },
    { "leftshoulder", "L1 (LB, L)", K_BUTTON },
    { "rightshoulder", "R1 (RB, R)", K_BUTTON },
    { "lefttrigger", "L2 (LT, ZL)", K_TRIGGER },
    { "righttrigger", "R2 (RT, ZR)", K_TRIGGER },
    { "leftx", "the left stick: push it RIGHT", K_STICK },
    { "lefty", "the left stick: push it DOWN", K_STICK },
    { "rightx", "the right stick: push it RIGHT", K_STICK },
    { "righty", "the right stick: push it DOWN", K_STICK },
    { "leftstick", "the left stick click (L3)", K_BUTTON },
    { "rightstick", "the right stick click (R3)", K_BUTTON },
};
const int kNumPrompts = (int)(sizeof(kPrompts) / sizeof(kPrompts[0]));

struct Raw
{
    std::vector<int> axis;
    std::vector<bool> button;
    std::vector<int> hat;
};

struct Ui
{
    bool open, close, focus;
    // the pad being set up; step -1 = none, kNumPrompts = all asked
    SDL_JoystickID id;
    SDL_Joystick *js;
    std::string name, guid;
    int step;
    Raw rest, prev;
    bool armed;
    std::vector<std::string> bind;
    std::string message;
    bool nav_off;
};

Ui g_ui;

void read_raw(SDL_Joystick *js, Raw &r)
{
    const int na = SDL_GetNumJoystickAxes(js), nb = SDL_GetNumJoystickButtons(js), nh = SDL_GetNumJoystickHats(js);
    r.axis.assign(na > 0 ? na : 0, 0);
    r.button.assign(nb > 0 ? nb : 0, false);
    r.hat.assign(nh > 0 ? nh : 0, 0);
    for (int i = 0; i < na; i++)
        r.axis[i] = SDL_GetJoystickAxis(js, i);
    for (int i = 0; i < nb; i++)
        r.button[i] = SDL_GetJoystickButton(js, i);
    for (int i = 0; i < nh; i++)
        r.hat[i] = SDL_GetJoystickHat(js, i);
}

bool at_rest(const Raw &r, const Raw &rest)
{
    for (size_t i = 0; i < r.button.size(); i++)
        if (r.button[i])
            return false;
    for (size_t i = 0; i < r.hat.size(); i++)
        if (r.hat[i])
            return false;
    for (size_t i = 0; i < r.axis.size() && i < rest.axis.size(); i++)
        if (std::abs(r.axis[i] - rest.axis[i]) > 8000)
            return false;
    return true;
}

bool bound(const std::string &input)
{
    for (size_t i = 0; i < g_ui.bind.size(); i++)
        if (g_ui.bind[i] == input)
            return true;
    return false;
}

// The input newly pressed for a prompt of this kind, in SDL's mapping notation, or "".
std::string detect(int kind, const Raw &r, const Raw &prev, const Raw &rest)
{
    char s[24];

    for (size_t i = 0; i < r.button.size(); i++)
        if (r.button[i] && !(i < prev.button.size() && prev.button[i])) {
            snprintf(s, sizeof(s), "b%d", (int)i);
            if (!bound(s))
                return s;
        }

    for (size_t i = 0; i < r.hat.size(); i++) {
        const int v = r.hat[i];
        if ((v == 1 || v == 2 || v == 4 || v == 8) && !(i < prev.hat.size() && prev.hat[i] == v)) {
            snprintf(s, sizeof(s), "h%d.%d", (int)i, v);
            if (!bound(s))
                return s;
        }
    }

    for (size_t i = 0; i < r.axis.size() && i < rest.axis.size(); i++) {
        const int d = r.axis[i] - rest.axis[i];
        const int pd = (i < prev.axis.size() ? prev.axis[i] : rest.axis[i]) - rest.axis[i];
        if (std::abs(d) <= 16000 || std::abs(pd) > 16000)
            continue;
        const int rv = rest.axis[i];
        if (kind == K_STICK)
            snprintf(s, sizeof(s), d > 0 ? "a%d" : "a%d~", (int)i);
        else if (kind == K_TRIGGER && rv < -24000)
            snprintf(s, sizeof(s), "a%d", (int)i);          // a trigger resting at one end
        else if (kind == K_TRIGGER && rv > 24000)
            snprintf(s, sizeof(s), "a%d~", (int)i);
        else
            snprintf(s, sizeof(s), d > 0 ? "+a%d" : "-a%d", (int)i);
        if (!bound(s))
            return s;
    }
    return std::string();
}

void stop_setup()
{
    if (g_ui.js)
        SDL_CloseJoystick(g_ui.js);
    g_ui.js = NULL;
    g_ui.step = -1;
    if (g_ui.nav_off) {
        // the pad steers the menus again
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        g_ui.nav_off = false;
    }
}

void start_setup(SDL_JoystickID id)
{
    stop_setup();
    g_ui.js = SDL_OpenJoystick(id);
    if (!g_ui.js) {
        g_ui.message = std::string("Couldn't open the pad: ") + SDL_GetError();
        return;
    }
    g_ui.id = id;
    const char *n = SDL_GetJoystickNameForID(id);
    g_ui.name = n ? n : "Gamepad";
    char guid[64];
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof(guid));
    g_ui.guid = guid;
    g_ui.bind.assign(kNumPrompts, std::string());
    read_raw(g_ui.js, g_ui.rest);        // the pad untouched (it was started with the mouse or keys)
    g_ui.prev = g_ui.rest;
    g_ui.armed = true;
    g_ui.step = 0;
    g_ui.message.clear();
    // while it is being set up its presses are answers, not menu moves
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    g_ui.nav_off = true;
}

void save_setup()
{
    std::string name = g_ui.name;
    for (size_t i = 0; i < name.size(); i++)
        if (name[i] == ',')
            name[i] = ' ';
    std::string m = g_ui.guid + "," + name + ",";
    int fields = 0;
    for (int i = 0; i < kNumPrompts; i++)
        if (!g_ui.bind[i].empty()) {
            m += std::string(kPrompts[i].field) + ":" + g_ui.bind[i] + ",";
            ++fields;
        }
    m += std::string("platform:") + SDL_GetPlatform() + ",";
    stop_setup();
    if (!fields)
        g_ui.message = "Nothing was pressed, so nothing was saved.";
    else if (zcport_sdl_gamepad_add_mapping(m.c_str()) == 0)
        g_ui.message = name + " is set up. It now uses the standard layout, in the game and the menus.";
    else
        g_ui.message = std::string("SDL didn't accept the layout: ") + SDL_GetError();
    al_trace("Gamepad Setup: %s\n", m.c_str());
}

// What a pad SDL knows is pressing, by the standard layout's names.
std::string standard_pressed(SDL_Gamepad *gp)
{
    static const int kBtn[18] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, -1, -2,
        SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
        SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
        SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_GUIDE,
        SDL_GAMEPAD_BUTTON_MISC1
    };
    std::string s;
    for (int i = 0; i < 18; i++) {
        bool down;
        if (kBtn[i] < 0)
            down = SDL_GetGamepadAxis(gp, kBtn[i] == -1 ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16384;
        else
            down = SDL_GetGamepadButton(gp, (SDL_GamepadButton)kBtn[i]);
        if (down) {
            if (!s.empty())
                s += ", ";
            s += kStdButtons[i];
        }
    }
    const int lx = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY);
    if (std::abs(lx) > 16384 || std::abs(ly) > 16384) {
        if (!s.empty())
            s += ", ";
        s += "left stick";
    }
    return s;
}

std::string raw_pressed(SDL_Joystick *js)
{
    Raw r;
    read_raw(js, r);
    std::string s;
    char b[32];
    for (size_t i = 0; i < r.button.size(); i++)
        if (r.button[i]) {
            snprintf(b, sizeof(b), "%sbutton %d", s.empty() ? "" : ", ", (int)i + 1);
            s += b;
        }
    for (size_t i = 0; i < r.hat.size(); i++)
        if (r.hat[i]) {
            snprintf(b, sizeof(b), "%shat %d", s.empty() ? "" : ", ", (int)i + 1);
            s += b;
        }
    return s;
}

// "A: Right face" for one of the game's actions with the current joystick buttons.
void action_line(const char *action, int btn)
{
    ImGui::TextDisabled("%-6s", action);
    ImGui::SameLine();
    if (btn >= 1 && btn <= 18)
        ImGui::TextUnformatted(kStdButtons[btn - 1]);
    else if (btn >= 1)
        ImGui::Text("button %d", btn);
    else
        ImGui::TextUnformatted("-");
}

void setup_layer(void *)
{
    Ui &u = g_ui;
    if (!u.open)
        return;
    const float sc = zcui::scale();
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                                                  IM_COL32(0, 0, 0, 120));
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(420 * sc, 0), ImVec2(vp->Size.x - 12 * sc, vp->Size.y - 12 * sc));
    if (u.focus) {
        ImGui::SetNextWindowFocus();
        u.focus = false;
    }
    bool open = true;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("Gamepad Setup###zplayer_gamepad", &open, flags)) {
        if (u.step >= 0 && u.js) {
            // ---- asking for one button after another ----
            Raw r;
            read_raw(u.js, r);
            if (u.step < kNumPrompts) {
                if (!u.armed)
                    u.armed = at_rest(r, u.rest);
                else {
                    const std::string in = detect(kPrompts[u.step].kind, r, u.prev, u.rest);
                    if (!in.empty()) {
                        u.bind[u.step++] = in;
                        u.armed = false;
                    }
                }
            }
            u.prev = r;

            ImGui::Text("Setting up: %s", u.name.c_str());
            ImGui::Separator();
            if (u.step < kNumPrompts) {
                ImGui::Text("Step %d of %d", u.step + 1, kNumPrompts);
                ImGui::Spacing();
                ImGui::Text("Press %s", kPrompts[u.step].label);
                if (!u.armed)
                    ImGui::TextDisabled("(let go of everything first)");
                ImGui::Spacing();
                if (ImGui::Button("Skip (Space)") || ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
                    u.bind[u.step++] = std::string();
                    u.armed = false;
                }
                ImGui::SameLine();
                if ((ImGui::Button("Back (Backspace)") || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && u.step > 0) {
                    u.bind[--u.step] = std::string();
                    u.armed = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                    stop_setup();
                ImGui::TextDisabled("Skip what the pad doesn't have (a PlayStation 1 pad has no sticks or L3/R3).");
            }
            else {
                ImGui::TextUnformatted("All asked. The answers:");
                for (int i = 0; i < kNumPrompts; i++)
                    if (!u.bind[i].empty())
                        ImGui::BulletText("%s: %s", kPrompts[i].field, u.bind[i].c_str());
                ImGui::Spacing();
                if (ImGui::Button("Save") || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
                    save_setup();
                ImGui::SameLine();
                if (ImGui::Button("Start over")) {
                    const SDL_JoystickID id = u.id;
                    start_setup(id);
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                    stop_setup();
            }
        }
        else {
            // ---- the connected pads ----
            int n = 0;
            SDL_JoystickID *ids = SDL_GetJoysticks(&n);
            if (!ids || n == 0)
                ImGui::TextUnformatted("No gamepad is connected. Plug one in and it shows here.");
            for (int i = 0; ids && i < n; i++) {
                ImGui::PushID((int)ids[i]);
                const char *name = SDL_GetJoystickNameForID(ids[i]);
                const bool known = SDL_IsGamepad(ids[i]);
                ImGui::SeparatorText(name ? name : "Gamepad");
                if (known)
                    ImGui::TextUnformatted("Ready: it uses the standard layout.");
                else
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                                       "Not set up: its buttons read as numbers. Set it up below.");
                std::string now;
                if (known) {
                    SDL_Gamepad *gp = SDL_GetGamepadFromID(ids[i]);
                    if (gp)
                        now = standard_pressed(gp);
                }
                else if (SDL_Joystick *js = SDL_GetJoystickFromID(ids[i]))
                    now = raw_pressed(js);
                ImGui::Text("Pressed: %s", now.empty() ? "-" : now.c_str());
                if (ImGui::Button(known ? "Set up again" : "Set up this pad"))
                    start_setup(ids[i]);
                if (known)
                    ImGui::SetItemTooltip("For a pad whose buttons read wrong: ask for each one and keep\n"
                                          "the answers in place of SDL's own layout.");
                ImGui::PopID();
            }
            SDL_free(ids);

            ImGui::SeparatorText("The game's buttons");
            ImGui::TextDisabled("Settings > Controls > Joystick Buttons changes them.");
            action_line("A", Abtn);
            action_line("B", Bbtn);
            action_line("Start", Sbtn);
            action_line("Map", Pbtn);
            action_line("Menu", Mbtn);      // ZC's own menu bar (as Esc)
            action_line("L", Lbtn);
            action_line("R", Rbtn);
            action_line("Ex1", Exbtn1);
            action_line("Ex2", Exbtn2);
            action_line("Ex3", Exbtn3);
            action_line("Ex4", Exbtn4);
            ImGui::TextDisabled("The D-pad and the left stick always move Link.");

            if (!u.message.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", u.message.c_str());
            }
            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(110 * sc, 0)))
                u.close = true;
            // Esc or the pad's B closes it (the window is the only thing up during the session)
            if (!ImGui::IsAnyItemActive() &&
                (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))
                u.close = true;
        }
    }
    ImGui::End();
    if (!open)
        u.close = true;
}

} // namespace

void zplayer_gamepad_setup()
{
    if (!zcui::available()) {
        al_trace("Gamepad Setup: the UI is not available\n");
        return;
    }
    {
        zcui::Lock lock;
        if (g_ui.open)
            return;
        g_ui.open = true;
        g_ui.close = false;
        g_ui.focus = true;
        g_ui.js = NULL;
        g_ui.step = -1;
        g_ui.nav_off = false;
        g_ui.message.clear();
    }
    const int layer = zcui::add_layer(setup_layer, NULL, 600);
    zcui::begin_session();
    for (;;) {
        zcui::wait_frame(100);
        bool done;
        {
            zcui::Lock lock;
            done = g_ui.close;
        }
        if (done || !zcui::available())
            break;
    }
    zcui::end_session();
    zcui::remove_layer(layer);
    {
        zcui::Lock lock;
        stop_setup();
        g_ui.open = false;
    }
}

// ==== test hook: ZPLAYER_GAMEPAD_TEST=1 opens the window 8 s after start and sets up the first
// pad SDL doesn't know (tests drive it through ZQ_UI_SCRIPT's virtual pads) ====
#include <chrono>
#include <thread>
namespace
{
void gamepad_test_thread()
{
    std::this_thread::sleep_for(std::chrono::seconds(8));
    {
        zcui::Lock lock;
        g_ui.step = -1;
    }
    std::thread([] {
        for (int t = 0; t < 100; ++t) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            zcui::Lock lock;
            if (!g_ui.open)
                continue;
            int n = 0;
            SDL_JoystickID *ids = SDL_GetJoysticks(&n);
            for (int i = 0; ids && i < n; i++)
                if (!SDL_IsGamepad(ids[i])) {
                    start_setup(ids[i]);
                    al_trace("Gamepad Setup test: setting up %s\n", g_ui.name.c_str());
                    break;
                }
            SDL_free(ids);
            return;
        }
    }).detach();
    zplayer_gamepad_setup();
    al_trace("Gamepad Setup test: closed (%s)\n", g_ui.message.c_str());
}
struct GamepadTestHook
{
    GamepadTestHook()
    {
        const char *e = getenv("ZPLAYER_GAMEPAD_TEST");
        if (e && e[0] == '1')
            std::thread(gamepad_test_thread).detach();
    }
} g_gamepad_test_hook;
}
// ==== test hook end ====

#endif // !__SWITCH__

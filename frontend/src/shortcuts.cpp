#include "shortcuts.h"

#include "prefs.h"

#include <cstdio>
#include <cstdlib>
#include <map>

namespace shortcuts {

namespace {

// config/settings.json, one key per kind of pad: "shortcut_button_<guid>".
std::string keyFor(SDL_Gamepad* gp) {
    char guid[64] = {};
    SDL_GUIDToString(SDL_GetJoystickGUID(SDL_GetGamepadJoystick(gp)), guid, sizeof guid);
    return std::string("shortcut_button_") + guid;
}

// READ ONCE AND KEPT. These are asked every frame for every pad while a game
// plays, and prefs reads its file on every call. Only this module writes them,
// so the copy cannot go stale.
int gEnabled = -1;                 // -1 until read
std::map<std::string, int> gChosen;  // key -> raw button, -1 for none

// The gamepad button a raw button is mapped to on this pad, or INVALID.
SDL_GamepadButton mappedTo(SDL_Gamepad* gp, int raw) {
    SDL_GamepadButton out = SDL_GAMEPAD_BUTTON_INVALID;
    int count = 0;
    SDL_GamepadBinding** binds = SDL_GetGamepadBindings(gp, &count);
    for (int i = 0; binds && i < count; ++i) {
        const SDL_GamepadBinding* b = binds[i];
        if (b->input_type == SDL_GAMEPAD_BINDTYPE_BUTTON && b->input.button == raw &&
            b->output_type == SDL_GAMEPAD_BINDTYPE_BUTTON) {
            out = b->output.button;
            break;
        }
    }
    SDL_free(binds);
    return out;
}

// SELECT, START, L3 AND R3 ARE NEVER THE SHORTCUT BUTTON (#313, MMagTech
// 2026-10-20): Select is the coin in every arcade game and Start the pause in
// most games, so a tap of either must reach the game; L3+R3 is the pause
// menu on every pad, and with L3 chosen the tap opened the menu and the R3
// closed it again. The row refuses them (pick), and a choice saved before
// this rule reads as none, so nobody is left with it.
bool isReserved(SDL_GamepadButton b) {
    return b == SDL_GAMEPAD_BUTTON_BACK || b == SDL_GAMEPAD_BUTTON_START ||
           b == SDL_GAMEPAD_BUTTON_LEFT_STICK || b == SDL_GAMEPAD_BUTTON_RIGHT_STICK;
}

// The raw button chosen for this kind of pad, or -1 for none.
int chosen(SDL_Gamepad* gp) {
    const std::string key = keyFor(gp);
    if (auto it = gChosen.find(key); it != gChosen.end()) return it->second;
    const std::string w = prefs::get(key, "");
    char* end = nullptr;
    const long n = w.empty() ? -1 : std::strtol(w.c_str(), &end, 10);
    int raw = (!w.empty() && end && *end == '\0' && n >= 0) ? static_cast<int>(n) : -1;
    if (raw >= 0 && isReserved(mappedTo(gp, raw))) raw = -1;
    gChosen[key] = raw;
    return raw;
}

std::string nameOf(SDL_Gamepad* gp, SDL_GamepadButton b) {
    switch (b) {
        case SDL_GAMEPAD_BUTTON_GUIDE: return "Home";
        case SDL_GAMEPAD_BUTTON_BACK: return "Select";
        case SDL_GAMEPAD_BUTTON_START: return "Start";
        case SDL_GAMEPAD_BUTTON_MISC1: return "Capture";
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L3";
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R3";
        case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return "Paddle";
        default: break;
    }
    const char* s = SDL_GetGamepadStringForButton(b);
    return s ? s : "";
}

}  // namespace

bool enabled() {
    if (gEnabled < 0) gEnabled = prefs::get("in_game_shortcuts", "off") == "on";
    return gEnabled == 1;
}

void setEnabled(bool on) {
    gEnabled = on;
    prefs::set("in_game_shortcuts", on ? "on" : "off");
}

// HOME ALWAYS WORKS, chosen button or not (#313, MMagTech 2026-10-20): a
// chosen button is added for a pad that has no Home, never put in Home's
// place. Until now choosing one replaced Home, so a stream's on-screen
// controls, whose only way in is the Home that Sunshine makes of a long
// Select, lost the menu whenever another button had been chosen for that
// kind of pad.
bool held(SDL_Gamepad* gp) {
    if (!gp) return false;
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_GUIDE)) return true;
    const int raw = chosen(gp);
    return raw >= 0 && SDL_GetJoystickButton(SDL_GetGamepadJoystick(gp), raw);
}

bool isShortcut(SDL_Gamepad* gp, SDL_GamepadButton b) {
    if (!gp) return false;
    if (b == SDL_GAMEPAD_BUTTON_GUIDE) return true;
    const int raw = chosen(gp);
    return raw >= 0 && mappedTo(gp, raw) == b;
}

std::string label(SDL_Gamepad* gp) {
    if (!gp) return "";
    const int raw = chosen(gp);
    if (raw < 0) return SDL_GamepadHasButton(gp, SDL_GAMEPAD_BUTTON_GUIDE) ? "Home" : "Not set";
    return label(gp, raw);
}

std::string label(SDL_Gamepad* gp, int raw) {
    if (!gp || raw < 0) return "";
    const SDL_GamepadButton b = mappedTo(gp, raw);
    if (b != SDL_GAMEPAD_BUTTON_INVALID) {
        const std::string n = nameOf(gp, b);
        if (!n.empty()) return n;
    }
    return "Button " + std::to_string(raw + 1);
}

// Whether a raw button drives an axis: ZL and ZR on a pad whose triggers are
// switches (a Switch Pro Controller's) are buttons mapped to trigger axes.
static bool drivesAxis(SDL_Gamepad* gp, int raw) {
    bool out = false;
    int count = 0;
    SDL_GamepadBinding** binds = SDL_GetGamepadBindings(gp, &count);
    for (int i = 0; binds && i < count; ++i)
        if (binds[i]->input_type == SDL_GAMEPAD_BINDTYPE_BUTTON &&
            binds[i]->input.button == raw &&
            binds[i]->output_type == SDL_GAMEPAD_BINDTYPE_AXIS)
            out = true;
    SDL_free(binds);
    return out;
}

Pick pick(SDL_Gamepad* gp, int raw) {
    if (!gp || raw < 0) return Pick::Ignore;
    if (drivesAxis(gp, raw)) return Pick::Ignore;
    switch (mappedTo(gp, raw)) {
        case SDL_GAMEPAD_BUTTON_EAST: return Pick::Cancel;
        case SDL_GAMEPAD_BUTTON_SOUTH:
        case SDL_GAMEPAD_BUTTON_WEST:
        case SDL_GAMEPAD_BUTTON_NORTH:
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return Pick::Ignore;
        case SDL_GAMEPAD_BUTTON_BACK:
        case SDL_GAMEPAD_BUTTON_START:
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return Pick::Refused;
        default: break;
    }
    prefs::set(keyFor(gp), std::to_string(raw));
    gChosen[keyFor(gp)] = raw;
    std::fprintf(stderr, "[shortcuts] %s: shortcut button is raw button %d (%s)\n",
                 SDL_GetGamepadName(gp) ? SDL_GetGamepadName(gp) : "pad", raw,
                 label(gp).c_str());
    return Pick::Taken;
}

}  // namespace shortcuts

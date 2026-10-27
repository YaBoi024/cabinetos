// The volume keys (#351, decided with MMagTech 2026-10-11).
//
// A TV console's volume is the television's, and the console never had a
// control of its own. Then a Steam Deck's rocker did nothing at all: the
// buttons send the standard keys (KEY_VOLUMEUP/KEY_VOLUMEDOWN, 115/114, from
// its built-in "AT Translated Set 2 keyboard") and nothing here listened.
//
// THE RULES:
//   - Volume keys from ANY keyboard step the system volume (the default sound
//     output, through WirePlumber's wpctl), silently: no bar, as with the
//     television's own volume.
//   - READ FROM THE KERNEL'S KEY DEVICES, NOT THE WINDOW, so they work while
//     a separate emulator (PS2, PS3, Switch, Xbox, Xbox 360, Wii U) has the
//     screen. Reading needs the seat's access grant, which keyboards do not
//     get by default: 71-cabinetos-key-devices.rules gives it.
//   - AT EVERY START THE VOLUME GOES BACK TO FULL, unless the machine has a
//     built-in screen (a DRM connector of type eDP, DSI or LVDS, which is what
//     Linux counts as an internal panel). So a keyboard can never leave a TV
//     console quiet with no way back for a controller, while a handheld keeps
//     its level.
//
// Not on an offscreen run: a headless frontend on the console would answer the
// same keys a second time and reset the television's volume.

#pragma once

#include <string>
#include <vector>

namespace volume {

// One press, in wpctl's percent.
inline constexpr int kStepPercent = 5;

// FORK (handheld): the level after the last key press, for the bar on screen
// (levelbar.h). generation() moves with each press; percent() is 0 to 100.
unsigned generation();
int percent();
bool muted();

// Starts the thread: the reset to full (unless a built-in screen), then the
// keys, for the rest of the run. Idempotent.
void start();

// --- The rules, pure, for --volume-test -----------------------------------------

// True when one of these DRM connector names ("card1-eDP-1", "card1-HDMI-A-1")
// is a built-in screen.
bool builtInScreen(const std::vector<std::string>& connectors);
// The connectors under /sys/class/drm, by name.
std::vector<std::string> connectors();

// True when a sysfs key list ("capabilities/key": hex words, most significant
// first, each the width of a long) has key `code`.
bool keyInBitmap(const std::string& bitmap, int code);

// The steps one key event makes: +1 for volume up, -1 for volume down, pressed
// or held (the kernel's repeat), 0 for anything else.
int stepOf(int type, int code, int value);

// wpctl's argument for a net change of `steps` presses: "15%+", "5%-".
std::string change(int steps);

// --volume-test: every rule above. 0 when all pass.
int test();

}  // namespace volume
